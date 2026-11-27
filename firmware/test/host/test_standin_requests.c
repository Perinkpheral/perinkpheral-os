/* SPDX-License-Identifier: MIT */
#define main capability_checks_main
#include "../../../sdk/check/checks.c"
#undef main
#include "tinytest.h"

static const standin_poll_t polls[] = {
    { .url = "https://example.test/result", .manual = true },
};
static const standin_answer_t answers[] = {
    { .address = "https://example.test/result", .status = 200, .body = "ok" },
};
const standin_subject_t standin_subject = {
    .polls = polls, .poll_count = 1, .answers = answers, .answer_count = 1,
    .request_presses = "UO",
};

void standin_subject_handlers(void) { }

static bool loading;

static void restore_result(void)
{
    loading = false;
}

static void request_on_up(int32_t event)
{
    if (event == BTN_UP_PRESSED) {
        loading = true;
        poll_run(0);
        screen_redraw(HOST_REDRAW_NORMAL);
    }
}

static void save_result(int32_t request, const uint8_t *data, int32_t length, int32_t flags, int32_t status)
{
    (void)request; (void)data; (void)length; (void)flags; (void)status;
    loading = false;
    kv_set("result", 6, "Ready", 5);
    screen_redraw(HOST_REDRAW_NORMAL);
}

static void draw_result(void)
{
    char result[6] = "Empty";
    kv_get("result", 6, result, sizeof result - 1);
    gfx_draw_text(0, 0, HOST_FONT_SMALL, loading ? "Loading" : result, loading ? 7 : 5);
}

static void retain_session(void)
{
    int32_t page = 0;
    session_set(&page, sizeof page);
}

static void check_request_restoration(void)
{
    standin_reset();
    standin_handlers = (standin_handlers_t){ .on_load = restore_result, .on_button = request_on_up,
        .on_data = save_result, .on_draw = draw_result, .on_blur = retain_session };
    CHECK(play(&ON_SCREEN));
    CHECK(standin()->polls_run[0] == 0);
    standin_reset();
    const scene_t whole = { .has_data = true };
    CHECK(play(&whole));
    CHECK(mark(MARK_MANUAL_DELIVERIES) > 0);
    uint64_t answered = mark(MARK_PICTURE);
    int32_t requested = standin()->polls_run[0];
    CHECK(standin()->session_length > 0);
    CHECK(play(&ON_SCREEN));
    CHECK(standin()->polls_run[0] == requested);
    CHECK(mark(MARK_PICTURE) == answered);
    standin_forget_session();
    CHECK(play(&ON_SCREEN));
    CHECK(standin()->polls_run[0] == requested);
    CHECK(mark(MARK_PICTURE) == answered);
}

static void check_request_preparation(void)
{
    standin_reset();
    standin_handlers = (standin_handlers_t){ .on_load = restore_result, .on_button = request_on_up,
        .on_data = save_result, .on_draw = draw_result };
    CHECK(play(&REQUESTS_PREPARED));
    CHECK(standin()->polls_run[0] == 1);
    CHECK(mark(MARK_MANUAL_DELIVERIES) == 0);
    const scene_t away = { .is_off_screen = true, .rounds = { { ROUND_WHOLE, 0, 0 } } };
    CHECK(play(&away));
    CHECK(mark(MARK_MANUAL_DELIVERIES) == 1);
    CHECK(play(&ON_SCREEN));
    CHECK(standin()->polls_run[0] == 1);
}

static void confirm_request(int32_t event)
{
    if (event == BTN_UP_PRESSED) {
        screen_redraw(HOST_REDRAW_QUESTION);
    } else if (event == BTN_OK_PRESSED) {
        poll_run(0);
    }
}

static void check_request_question(void)
{
    standin_reset();
    standin_handlers = (standin_handlers_t){ .on_button = confirm_request,
        .on_data = save_result, .on_draw = draw_result };
    const scene_t whole = { .has_data = true };
    CHECK(play(&whole));
    CHECK(mark(MARK_MANUAL_DELIVERIES) > 0);
}

int main(void)
{
    check_request_restoration();
    check_request_preparation();
    check_request_question();
    REPORT();
}
