/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "tinytest.h"
#include "freertos/task.h"
#include "esp_system.h"

/* Exercise the actual host button and frame paths, with no task or panel. */
#include "../../main/host.c"
#include "freertos/queue.h"
#include "freertos/semphr.h"

struct test_queue {
    event_t events[64];
    unsigned length;
    unsigned count;
    unsigned first;
};
static struct test_queue queues[2];
static unsigned queue_count;
static int64_t uptime_ms;
static display_result_t panel_result;
static unsigned panel_calls, draw_calls, power_presses, switches;
static scene_t home, application, neighbour;

QueueHandle_t xQueueCreate(unsigned length, unsigned item_size)
{
    CHECK(item_size == sizeof(event_t));
    CHECK(queue_count < 2 && length <= 64);
    struct test_queue *queue = &queues[queue_count++];
    queue->length = length;
    return queue;
}
QueueSetHandle_t xQueueCreateSet(unsigned length) { (void)length; return queues; }
int xQueueAddToSet(QueueHandle_t queue, QueueSetHandle_t set)
{
    (void)queue; (void)set; return pdTRUE;
}
int xQueueSend(QueueHandle_t queue, const void *item, unsigned wait)
{
    (void)wait;
    if (queue->count == queue->length) return pdFALSE;
    queue->events[(queue->first + queue->count++) % queue->length] = *(const event_t *)item;
    return pdTRUE;
}
int xQueueReceive(QueueHandle_t queue, void *item, unsigned wait)
{
    (void)wait;
    if (queue->count == 0) return pdFALSE;
    *(event_t *)item = queue->events[queue->first];
    queue->first = (queue->first + 1) % queue->length;
    queue->count--;
    return pdTRUE;
}
unsigned uxQueueMessagesWaiting(QueueHandle_t queue) { return queue->count; }
unsigned uxQueueSpacesAvailable(QueueHandle_t queue) { return queue->length - queue->count; }
QueueHandle_t xQueueSelectFromSet(QueueSetHandle_t set, unsigned wait)
{
    (void)set; (void)wait;
    return queues[0].count ? &queues[0] : queues[1].count ? &queues[1] : NULL;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return queues; }
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return queues; }
int xSemaphoreTake(SemaphoreHandle_t semaphore, unsigned wait)
{
    (void)semaphore; (void)wait; return pdTRUE;
}
void xSemaphoreGive(SemaphoreHandle_t semaphore) { (void)semaphore; }
void vSemaphoreDelete(SemaphoreHandle_t semaphore) { (void)semaphore; }
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return queues; }
int64_t esp_timer_get_time(void) { return uptime_ms * US_PER_MS; }

void power_note_press(void) { power_presses++; }
void host_notices_note_press(void) {}
void host_notices_note_going_home(bool going) { (void)going; }
void app_delivery_note_press(app_delivery_state_t *state, int64_t at_ms)
{
    (void)state; (void)at_ms;
}
void app_delivery_abort(app_delivery_state_t *state, wm_t *wm, int64_t at_ms,
                         app_abandoned_t *abandoned)
{
    (void)state; (void)wm; (void)at_ms;
    *abandoned = (app_abandoned_t){0};
}
const char *launcher_app_name(const scene_t *scene) { (void)scene; return NULL; }
void poller_answer_was_abandoned(const char *name, int request) { (void)name; (void)request; }
scene_t *scene_menu_neighbour(const scene_t *scene, int direction)
{
    (void)scene; (void)direction; switches++; return &neighbour;
}
bool scene_menu_lists(const scene_t *scene) { (void)scene; return true; }

uint32_t display_wait_ms(refresh_reason_t reason) { (void)reason; return 0; }
display_result_t display_show(const uint8_t *frame, refresh_reason_t reason)
{
    (void)frame; (void)reason;
    panel_calls++;
    uptime_ms += 4000; /* The retry pause starts when the failed refresh finishes. */
    return panel_result;
}
static void draw(scene_t *scene, gfx_t *surface)
{
    (void)scene; (void)surface; draw_calls++;
}
static scene_reaction_t button(scene_t *scene, scene_button_t button, scene_press_t press)
{
    (void)scene; (void)button; (void)press;
    return (scene_reaction_t){ .kind = SCENE_IGNORED };
}
static void empty_header(gfx_t *surface, const scene_t *scene, void *context)
{
    (void)surface; (void)scene; (void)context;
}
static const scene_ops_t operations = { .draw = draw, .button = button };

static void start_scene(void)
{
    home = (scene_t){ .ops = &operations, .fullscreen = true };
    application = (scene_t){ .ops = &operations, .fullscreen = true };
    neighbour = (scene_t){ .ops = &operations, .fullscreen = true };
    wm_init(&host_wm, &home, &application, empty_header, NULL);
}
static void tap(uint8_t index)
{
    events_post_button(index, EVENT_PRESS_DOWN);
    events_post_button(index, EVENT_PRESS_UP);
    events_post_button(index, EVENT_PRESS_SHORT);
}
static void drain_buttons(void)
{
    event_t event;
    while (events_wait(&event, 0)) {
        CHECK(event.kind == EVENT_BUTTON);
        on_button(&host_wm, event.button.index, event.button.press, event.button.at_ms,
                   event.button.long_held);
    }
}
static void test_saturated_long_back(bool chorded)
{
    start_scene();
    switches = 0;
    events_post_button(3, EVENT_PRESS_DOWN);
    if (chorded) tap(0);
    for (unsigned press = 0; press < 4; press++) tap(1);
    CHECK(uxQueueMessagesWaiting(&queues[1]) == 12);
    events_post_button(3, EVENT_PRESS_LONG);
    CHECK(uxQueueMessagesWaiting(&queues[1]) == 12); /* LONG was refused. */
    events_post_button(3, EVENT_PRESS_UP);
    CHECK(uxQueueMessagesWaiting(&queues[1]) == 13);
    drain_buttons();
    CHECK(host_wm.current == (chorded ? &neighbour : &home));
    CHECK(switches == (chorded ? 1u : 0u));
    /* The next press starts cleanly, including after a chord's lost LONG. */
    events_post_button(3, EVENT_PRESS_DOWN);
    events_post_button(3, EVENT_PRESS_LONG);
    events_post_button(3, EVENT_PRESS_UP);
    drain_buttons();
    CHECK(host_wm.current == &home);
}
static void test_short_and_injected_back(void)
{
    start_scene();
    tap(3);
    drain_buttons();
    CHECK(host_wm.current == &application);
    events_post_button(3, EVENT_PRESS_DOWN);
    events_post_button(3, EVENT_PRESS_LONG);
    drain_buttons();
    CHECK(host_wm.current == &application); /* A held BACK can still become a chord. */
    events_post_button(3, EVENT_PRESS_UP);
    drain_buttons();
    CHECK(host_wm.current == &home);
    start_scene();
    events_post_button(3, EVENT_PRESS_LONG); /* A complete press injected by a tool. */
    drain_buttons();
    CHECK(host_wm.current == &home);
}
static void test_refused_down_has_no_release(void)
{
    for (unsigned press = 0; press < 4; press++) tap(1);
    CHECK(uxQueueMessagesWaiting(&queues[1]) == 12);
    events_post_button(3, EVENT_PRESS_DOWN);
    events_post_button(3, EVENT_PRESS_LONG);
    events_post_button(3, EVENT_PRESS_UP);
    CHECK(uxQueueMessagesWaiting(&queues[1]) == 12);
    drain_buttons();
    /* Nor can a refused hold mark the next short BACK as long. */
    events_post_button(3, EVENT_PRESS_DOWN);
    events_post_button(3, EVENT_PRESS_UP);
    event_t event;
    CHECK(events_wait(&event, 0) && event.button.press == EVENT_PRESS_DOWN);
    CHECK(events_wait(&event, 0) && event.button.press == EVENT_PRESS_UP);
    CHECK(!event.button.long_held);
    on_button(&host_wm, 3, EVENT_PRESS_DOWN, uptime_ms, false);
    on_button(&host_wm, 3, EVENT_PRESS_UP, uptime_ms, false);
}
static void test_panel_backoff_survives_events(void)
{
    start_scene();
    gfx_t screen;
    gfx_init(&screen, s_frame, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    panel_result = DISPLAY_FAILED;
    uptime_ms = 1000;
    CHECK(show_pending_frame(&host_wm, &screen) == RETRY_FAILED_SHOW_MS);
    CHECK(panel_calls == 1 && draw_calls == 1);
    CHECK(host_frame_is_pending());
    CHECK(host_frames_shown() == 0);
    int64_t failed_at_ms = uptime_ms;
    unsigned presses_before = power_presses;
    for (unsigned second = 1; second < 30; second++) {
        uptime_ms = failed_at_ms + second * 1000;
        tap(1);
        drain_buttons();
        wm_invalidate(&host_wm, WM_CHANGED_BY_DATA); /* A routine status/deadline wake. */
        CHECK(show_pending_frame(&host_wm, &screen) == 30000 - second * 1000);
        CHECK(panel_calls == 1 && draw_calls == 1);
    }
    CHECK(power_presses > presses_before);
    /* A different scene and an urgent frame still respect the panel's pause. */
    wm_open_from_home(&host_wm, &neighbour);
    CHECK(show_pending_frame(&host_wm, &screen) == 1000);
    uptime_ms = failed_at_ms + RETRY_FAILED_SHOW_MS;
    CHECK(show_pending_frame(&host_wm, &screen) == RETRY_FAILED_SHOW_MS);
    CHECK(panel_calls == 2 && host_frame_is_pending());
    uptime_ms += RETRY_FAILED_SHOW_MS - 1;
    CHECK(show_pending_frame(&host_wm, &screen) == 1);
    CHECK(panel_calls == 2);
    uptime_ms++;
    panel_result = DISPLAY_SHOWN;
    CHECK(show_pending_frame(&host_wm, &screen) == NOTHING_TO_SHOW);
    CHECK(panel_calls == 3 && host_frames_shown() == 1);
    CHECK(!host_frame_is_pending());
    wm_invalidate(&host_wm, WM_CHANGED_BY_INPUT);
    panel_result = DISPLAY_UNCHANGED;
    CHECK(show_pending_frame(&host_wm, &screen) == NOTHING_TO_SHOW);
    CHECK(panel_calls == 4 && host_frames_shown() == 1);
}
int main(void)
{
    events_start();
    test_saturated_long_back(false);
    test_saturated_long_back(true);
    test_short_and_injected_back();
    test_refused_down_has_no_release();
    test_panel_backoff_survives_events();
    REPORT();
}
