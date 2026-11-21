/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "question_press.h"

void question_press_shown(question_press_t *state, const scene_t *scene, int64_t at_ms)
{
    if (state->shown_scene != scene || state->shown_question != scene->question) {
        state->shown_at_ms = at_ms;
    }
    state->shown_scene = scene;
    state->shown_question = scene->question;
}

bool question_press_accepts(question_press_t *state, const scene_t *scene, scene_button_t button,
                            scene_press_t press, int64_t at_ms)
{
    uint8_t bit = (uint8_t)(1u << button);
    bool is_held = scene->asks_once || (scene->question != 0 && button == SCENE_BUTTON_OK);
    if (!is_held) {
        /* Another button cannot invalidate an OK held on the same question. */
        state->went_down &= (uint8_t)~bit;
        return true;
    }
    if (press == SCENE_PRESS_DOWN) {
        if (state->shown_scene == scene && state->shown_question == scene->question &&
            at_ms >= state->shown_at_ms) {
            state->went_down |= bit;
            state->began_on[button].scene = scene;
            state->began_on[button].question = scene->question;
        } else {
            state->went_down &= (uint8_t)~bit;
        }
        return false;
    }
    bool began_here = (state->went_down & bit) != 0 && state->began_on[button].scene == scene &&
                      state->began_on[button].question == scene->question;
    if (press == SCENE_PRESS_SHORT || press == SCENE_PRESS_LONG) {
        state->went_down &= (uint8_t)~bit;
    }
    return began_here;
}
