/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_delivery.h"

#include "app_scene.h"
#include "host_api.h"

void app_delivery_init(app_delivery_state_t *state)
{
    state->in_background = NULL;
    state->is_mid_answer = false;
    state->answer_owner = NULL;
    state->answer_instance = 0;
    state->has_given_focus = false;
    state->last_focus_ms = 0;
    state->last_press_ms = 0;
    state->settings_owner = NULL;
    state->settings_ms = 0;
}

/* Returns whether the instance takes the rest of an answer given piece by piece. */
static bool call_handler(app_t *app, const app_delivery_t *delivery)
{
    if (delivery->kind == APP_DELIVERY_ALARM) {
        app_alarm(app, delivery->request);
    } else if (delivery->kind == APP_DELIVERY_INTENT) {
        app_intent(app, delivery->request);
    } else if (delivery->kind == APP_DELIVERY_SETTINGS) {
        app_settings_changed(app);
    } else if (delivery->kind == APP_DELIVERY_INSTALLED) {
        app_installed(app);
    } else if (delivery->kind == APP_DELIVERY_UPDATED) {
        app_updated(app);
    } else if (delivery->kind == APP_DELIVERY_VALUES) {
        app_values(app, delivery->request, delivery->http_status, delivery->picked);
    } else if (delivery->headers_only) {
        return app_data_headers(app, delivery->request, delivery->http_status);
    } else {
        return app_data(app, delivery->request, delivery->data, delivery->length, delivery->flags,
                        delivery->http_status);
    }
    return true;
}

static bool call(app_t *app, const app_delivery_t *delivery)
{
    if (delivery->loaded != NULL) {
        delivery->loaded(delivery->loaded_context);
    }
    bool is_answer = delivery->kind == APP_DELIVERY_PIECE ||
                        delivery->kind == APP_DELIVERY_VALUES;
    if (is_answer) {
        app_answer_begins(app, delivery->response, delivery->headers, delivery->http_status,
                          delivery->has_item ? delivery->item : -1);
    }
    bool takes_rest = call_handler(app, delivery);
    if (is_answer) {
        app_answer_ends(app);
    }
    return takes_rest;
}

/* Nothing more of this answer will come. */
static bool is_last(const app_delivery_t *delivery)
{
    return delivery->kind != APP_DELIVERY_PIECE ||
           (delivery->flags & (HOST_DATA_LAST | HOST_DATA_FAILED)) != 0;
}

void app_delivery_note_press(app_delivery_state_t *state, int64_t now_ms)
{
    state->last_press_ms = now_ms;
}

/* Not more often than every so long; not while the user is pressing buttons; and never over a
 * question waiting for its answer. */
static bool may_give_focus(const app_delivery_state_t *state, const wm_t *wm, int64_t now_ms)
{
    const scene_t *shown = wm->current;
    bool is_asking = shown != NULL && (shown->asks_once || shown->question != 0);
    bool is_user_here = state->last_press_ms != 0 && now_ms - state->last_press_ms < APP_FOCUS_AFTER_PRESS_MS;
    return !is_asking && !is_user_here &&
           (!state->has_given_focus || now_ms - state->last_focus_ms >= APP_FOCUS_MIN_GAP_MS);
}

static void end_background(app_delivery_state_t *state, wm_t *wm, app_t *app, int64_t now_ms)
{
    scene_t *owner = state->in_background;
    bool wants_screen = app != NULL && app_take_focus_request(app) && !app_has_stopped(app);
    app_scene_end_background(owner);
    state->in_background = NULL;
    state->is_mid_answer = false;
    if (wants_screen && may_give_focus(state, wm, now_ms)) {
        state->has_given_focus = true;
        state->last_focus_ms = now_ms;
        wm_open(wm, owner);
    } else {
        wm_resume(wm);
    }
}

static app_abandoned_t abandon_answer(app_delivery_state_t *state)
{
    app_abandoned_t abandoned = { .owner = state->answer_owner, .request = state->answer_request };
    state->answer_instance = 0;
    return abandoned;
}

static app_abandoned_t check_instance(app_delivery_state_t *state)
{
    if (state->answer_instance != 0 &&
        state->answer_instance != app_scene_instance(state->answer_owner)) {
        return abandon_answer(state);
    }
    return (app_abandoned_t){0};
}

static void note_piece(app_delivery_state_t *state, scene_t *owner,
                       const app_delivery_t *delivery)
{
    if (delivery->kind != APP_DELIVERY_PIECE) {
        return;
    }
    state->answer_owner = is_last(delivery) ? NULL : owner;
    state->answer_instance = is_last(delivery) ? 0 : app_scene_instance(owner);
    state->answer_request = delivery->request;
}

void app_delivery_abort(app_delivery_state_t *state, wm_t *wm, int64_t now_ms,
                           app_abandoned_t *abandoned)
{
    *abandoned = check_instance(state);
    if (state->in_background != NULL) {
        if (state->is_mid_answer) {
            *abandoned = abandon_answer(state);
        }
        end_background(state, wm, NULL, now_ms);
    }
}

app_abandoned_t app_deliver(app_delivery_state_t *state, wm_t *wm, scene_t *owner,
                            const app_delivery_t *delivery, int64_t now_ms)
{
    app_abandoned_t abandoned = check_instance(state);
    /* Continuations belong to the instance that received FIRST. A failed
     * response may also start without FIRST, unless this stream was lost. */
    bool is_first_piece = delivery->kind != APP_DELIVERY_PIECE ||
                          ((delivery->flags & HOST_DATA_FIRST) != 0 && !delivery->is_closing) ||
                          ((delivery->flags & HOST_DATA_FAILED) != 0 && !delivery->is_closing &&
                           !(state->answer_owner == owner && state->answer_request == delivery->request));
    if (!is_first_piece && (state->answer_owner != owner ||
                           state->answer_request != delivery->request || state->answer_instance == 0)) {
        if (state->answer_owner == owner && state->answer_request == delivery->request && is_last(delivery)) {
            state->answer_owner = NULL;
        }
        abandoned.rest_unwanted = true;
        return abandoned;
    }
    if (delivery->kind == APP_DELIVERY_SETTINGS && state->answer_owner == owner &&
        state->answer_request < HOST_REQUEST_FIRST) {
        state->answer_owner = NULL;
        state->answer_instance = 0;
    }
    bool is_on_screen = wm->current == owner && !wm->suspended;
    if (is_on_screen) {
        app_t *app = app_scene_loaded_app(owner);
        abandoned.rest_unwanted = app == NULL;
        if (app != NULL) {
            abandoned.rest_unwanted = !call(app, delivery);
            note_piece(state, owner, delivery);
            /* On the screen there is nothing to ask for. */
            app_take_focus_request(app);
            /* Somebody has just saved its settings and is looking at the
             * device: what the settings changed is shown at once, and so is
             * the answer fetched because of them - not when the panel is
             * next due for data. */
            if (delivery->kind == APP_DELIVERY_SETTINGS) {
                state->settings_owner = owner;
                state->settings_ms = now_ms;
            }
            bool is_after_settings = state->settings_owner == owner &&
                                     now_ms - state->settings_ms < APP_SETTINGS_SHOWN_SOON_MS;
            wm_scene_reacted(wm, owner, app_scene_take_reaction(owner),
                             is_after_settings ? WM_CHANGED_URGENTLY : WM_CHANGED_BY_DATA);
        }
        /* It came onto the screen while it was loaded for an answer - the
         * user opened it: it is the scene's now, and not to be unloaded as
         * a background one when the next answer comes for somebody else. */
        if (state->in_background == owner) {
            state->in_background = NULL;
            state->is_mid_answer = false;
        }
        return abandoned;
    }

    if (delivery->only_on_screen) {
        abandoned.rest_unwanted = true;
        return abandoned;
    }
    /* The rest of the old poll will be suppressed. Start the settings
     * callback in a fresh instance and keep the foreground suspended. */
    if (delivery->kind == APP_DELIVERY_SETTINGS && state->in_background == owner &&
        state->is_mid_answer && state->answer_request < HOST_REQUEST_FIRST) {
        app_scene_end_background(owner);
        state->in_background = NULL;
        state->is_mid_answer = false;
    }
    app_t *app;
    if (state->in_background == owner) {
        app = app_scene_loaded_app(owner);
    } else {
        /* An answer for another application while one is still taking its
         * own does not happen - requests go one at a time - but if it did,
         * the first one is not left loaded. */
        if (state->in_background != NULL) {
            end_background(state, wm, NULL, now_ms);
        }
        wm_suspend(wm);
        app_abandoned_t interrupted = check_instance(state);
        if (interrupted.owner != NULL) {
            abandoned = interrupted;
        }
        app = app_scene_begin_background(owner);
        state->in_background = owner;
    }
    abandoned.rest_unwanted = app == NULL || !call(app, delivery);
    if (app == NULL && app_scene_needs_memory(owner) &&
        (delivery->kind == APP_DELIVERY_PIECE || delivery->kind == APP_DELIVERY_VALUES)) {
        abandoned = (app_abandoned_t){ .owner = owner, .request = delivery->request,
                                       .rest_unwanted = true };
    }
    note_piece(state, owner, delivery);
    if (delivery->kind == APP_DELIVERY_PIECE) {
        state->is_mid_answer = app != NULL && !is_last(delivery);
        if (app == NULL && !is_last(delivery)) {
            state->answer_instance = 0;
        }
    }
    /* Something else for it in the middle of an answer - its settings
     * changed, its alarm came - is taken by the instance that is taking
     * the answer; unloading it now would lose what is still to come. */
    if (!state->is_mid_answer) {
        end_background(state, wm, app, now_ms);
    }
    return abandoned;
}
