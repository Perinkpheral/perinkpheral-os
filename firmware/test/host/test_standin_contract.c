/* SPDX-License-Identifier: MIT */
#include "tinytest.h"
#include "standin.h"
#include "app_ui.h"
#include "app_alloc.h"
#include "app_state.h"

APP_ALLOC_ARENA(64);
static const char *const hosts[] = { "example.test" };

#ifndef STANDIN_FULLSCREEN
#define STANDIN_FULLSCREEN 0
#endif

static const standin_poll_t polls[] = {
    { .url = "https://example.test/manual", .manual = true },
    { .url = "https://example.test/periodic" },
};
static const char *const certificates[] = { "root.pem" };
const standin_subject_t standin_subject = {
    .writes_files = true,
    .polls = polls,
    .poll_count = 2,
    .is_fullscreen = STANDIN_FULLSCREEN,
    .hosts = hosts, .host_count = 1,
    .certificates = certificates,
    .certificate_count = 1,
};

static int32_t button_events[4];
static int32_t button_count;

static void on_button(int32_t event)
{
    if (button_count < (int32_t)(sizeof button_events / sizeof button_events[0])) {
        button_events[button_count++] = event;
    }
}

static void on_draw(void)
{
    CHECK(standin()->redraw_flags == 0);
    screen_redraw(HOST_REDRAW_CLEAN);
}

static void test_package_files(void)
{
    standin_reset();
    const char *const protected[] = { "app.wasm", "manifest.json", "manifest.ru", "manifest.other", "root.pem" };
    for (size_t index = 0; index < sizeof protected / sizeof protected[0]; index++) {
        const char *name = protected[index];
        CHECK(file_write(name, (int32_t)strlen(name), 0, "x", 1) == HOST_ERR_ARGUMENT);
        CHECK(file_write(name, (int32_t)strlen(name), 0, NULL, 0) == HOST_ERR_ARGUMENT);
        CHECK(file_size(name, (int32_t)strlen(name)) == HOST_ERR_NOT_FOUND);
    }
    CHECK(standin()->file_writes == 0);
    CHECK(file_write("manifest", 8, 0, "x", 1) == HOST_OK);
    CHECK(file_write("root.pem.copy", 13, 0, "x", 1) == HOST_OK);
}

static void test_time_text_zero_capacity(void)
{
    standin_reset();
    char text[] = { 'x', 'y' };
    for (int32_t style = HOST_TIME_CLOCK; style <= HOST_TIME_WEEKDAY_SHORT; style++) {
        CHECK(time_text(HOST_TIME_MIN, style, text, 0) == 0);
        CHECK(time_text(HOST_TIME_MAX, style, text, 0) == 0);
    }
    CHECK(time_text(HOST_TIME_MIN - 1, HOST_TIME_CLOCK, text, 0) == HOST_ERR_ARGUMENT);
    CHECK(time_text(HOST_TIME_MAX + 1, HOST_TIME_CLOCK, text, 0) == HOST_ERR_ARGUMENT);
    CHECK(time_text(HOST_TIME_MIN, -1, text, 0) == HOST_ERR_ARGUMENT);
    CHECK(time_text(HOST_TIME_MIN, HOST_TIME_WEEKDAY_SHORT + 1, text, 0) == HOST_ERR_ARGUMENT);
    CHECK(time_text(HOST_TIME_MIN, HOST_TIME_CLOCK, text, -1) == HOST_ERR_ARGUMENT);
    CHECK(time_text(HOST_TIME_MIN, HOST_TIME_CLOCK, NULL, 0) == HOST_ERR_ARGUMENT);
    CHECK(text[0] == 'x' && text[1] == 'y');
}

static void test_long_back(void)
{
    standin_reset();
    standin_handlers = (standin_handlers_t){ .on_button = on_button };
    standin_load();
    standin_focus();
    button_count = 0;
    standin_long_press(BTN_BACK);
    CHECK(button_count == 1);
    CHECK(button_events[0] == BTN_BACK_KEYDOWN);
    CHECK(standin()->is_leaving && !standin()->is_loaded);
    standin_long_press(BTN_UP);
    CHECK(button_count == 1);
    standin_load();
    standin_focus();
    button_count = 0;
    standin_long_press(BTN_UP);
    CHECK(button_count == 3);
    CHECK(button_events[0] == BTN_UP_KEYDOWN);
    CHECK(button_events[1] == BTN_UP_LONG_PRESSED);
    CHECK(button_events[2] == BTN_UP_KEYUP);
    button_count = 0;
    standin_press(BTN_BACK);
    CHECK(button_count == 2);
    CHECK(button_events[1] == BTN_BACK_KEYUP);
}

static void test_button_encoding(void)
{
    const int32_t events[][4] = {
        { 16, 17, 18, 19 }, { 32, 33, 34, 35 },
        { 48, 49, 50, 51 }, { 64, 65, 66, 67 },
    };
    standin_reset();
    standin_handlers = (standin_handlers_t){ .on_button = on_button };
    for (int32_t button = BTN_UP; button <= BTN_BACK; button++) {
        for (int32_t action = 0; action < 16; action++) {
            int32_t event = BTN_EVENT(button, action);
            CHECK(event == button * 16 + action);
            CHECK(BTN_OF(event) == button && BTN_ACTION_OF(event) == action);
        }
        standin_load();
        standin_focus();
        button_count = 0;
        standin_press(button);
        CHECK(button_count == (button == BTN_BACK ? 2 : 3));
        for (int32_t action = 0; action < button_count; action++) {
            CHECK(button_events[action] == events[button - 1][action]);
        }
        standin_load();
        standin_focus();
        button_count = 0;
        standin_long_press(button);
        CHECK(button_events[0] == events[button - 1][BTN_KEYDOWN]);
        if (button != BTN_BACK) {
            CHECK(button_events[1] == events[button - 1][BTN_LONG_PRESSED]);
            CHECK(button_events[2] == events[button - 1][BTN_KEYUP]);
        }
    }
}

static void test_time_ranges(void)
{
    standin_reset();
    standin()->zone_offset = 3600;
    const int64_t moments[] = { 0, 15681600, 946684800, HOST_CLOCK_MIN - 1,
                                HOST_CLOCK_MIN, HOST_TIME_MAX };
    for (size_t index = 0; index < sizeof moments / sizeof moments[0]; index++) {
        host_local_time_t parts;
        int64_t restored = INT64_MIN;
        char text[5];
        CHECK(time_local_at(moments[index], &parts) == HOST_OK);
        CHECK(time_from_local(&parts, &restored) == HOST_OK && restored == moments[index]);
        CHECK(time_text(moments[index], HOST_TIME_CLOCK, text, sizeof text) == sizeof text);
    }
    CHECK(alarm_set_at(0, HOST_CLOCK_MIN - 1) == HOST_ERR_ARGUMENT);
    host_local_time_t untouched = { .year = 12345 };
    char unchanged[] = "keep";
    CHECK(time_local_at(-1, &untouched) == HOST_ERR_ARGUMENT && untouched.year == 12345);
    CHECK(time_text(-1, HOST_TIME_CLOCK, unchanged, sizeof unchanged) == HOST_ERR_ARGUMENT &&
          memcmp(unchanged, "keep", sizeof unchanged) == 0);
    CHECK(alarm_set_at(0, HOST_CLOCK_MAX + 1) == HOST_ERR_ARGUMENT);
    CHECK(alarm_set_at(0, HOST_CLOCK_MIN) == HOST_OK);
    CHECK(alarm_set_at(0, HOST_CLOCK_MAX) == HOST_OK);
    const int64_t unknown[] = { INT64_MIN, HOST_TIME_MIN, -1, 0, HOST_CLOCK_MIN - 1,
                                HOST_CLOCK_MAX + 1, INT64_MAX };
    for (size_t index = 0; index < sizeof unknown / sizeof unknown[0]; index++) {
        standin_clock(unknown[index]);
        host_local_time_t parts = { .year = 12345 };
        CHECK(time_now() == 0 && time_zone_offset() == 0);
        CHECK(time_local(&parts) == HOST_ERR_NOT_FOUND && parts.year == 12345);
        CHECK(standin()->now == unknown[index]); /* raw test clock is retained */
    }
    const int64_t known[] = { HOST_CLOCK_MIN, HOST_CLOCK_MAX };
    for (size_t index = 0; index < sizeof known / sizeof known[0]; index++) {
        standin_clock(known[index]);
        host_local_time_t parts;
        CHECK(time_now() == known[index] && time_zone_offset() == 3600);
        CHECK(time_local(&parts) == HOST_OK);
    }
    /* Explicit 1970 conversions remain available with an unknown current clock. */
    standin_clock(-1);
    char text[16] = { 0 };
    CHECK(time_text(0, HOST_TIME_NEAR, text, sizeof text) == 5 && memcmp(text, "1 Jan", 5) == 0);
    CHECK(time_text(0, HOST_TIME_NEAR_DAY, text, sizeof text) == 5 && memcmp(text, "1 Jan", 5) == 0);
    standin_clock(HOST_CLOCK_MAX);
    standin_pass(1000);
    CHECK(time_now() == 0 && standin()->now == HOST_CLOCK_MAX + 1 && time_uptime_ms() == 1000);
    standin_clock(HOST_CLOCK_MIN - 1);
    standin_pass(1000);
    CHECK(time_now() == HOST_CLOCK_MIN);
}

static void test_recurring_phase(void)
{
    const int64_t corrections[] = { 30, -30, 86400, -86400 };
    for (size_t index = 0; index < sizeof corrections / sizeof corrections[0]; index++) {
        standin_reset();
        CHECK(alarm_set_every(0, 60, 15) == HOST_OK);
        standin_pass(20000);
        standin_clock(time_now() + corrections[index]);
        CHECK(standin_alarm_due(0));
        standin_alarm(0);
        CHECK(standin_alarm_left(0) == 55);
        standin_pass(55000);
        standin_alarm(0);
        CHECK(standin_alarm_left(0) == 60);
        standin_pass(180000);
        standin_alarm(0);
        CHECK(standin_alarm_left(0) == 60);
    }
}

static void test_log_sanitization(void)
{
    standin_reset();
    const char bytes[] = { 'A', '\n', '\0', '\x1f', '\x7f', '\xc2', '\xb0', '~' };
    log_write(HOST_LOG_INFO, bytes, sizeof bytes);
    CHECK_STR(standin()->last_log, "A??????~");
    uint64_t diagnostics = standin()->diagnostics;
    standin_reset();
    log_write(HOST_LOG_INFO, "A??????~", sizeof bytes);
    CHECK(standin()->diagnostics == diagnostics);
    char long_line[HOST_LOG_TEXT_MAX + 1];
    memset(long_line, '\n', sizeof long_line);
    log_write(HOST_LOG_WARN, long_line, sizeof long_line);
    CHECK(strlen(standin()->last_log) == HOST_LOG_TEXT_MAX);
    CHECK(standin()->last_log[HOST_LOG_TEXT_MAX - 1] == '?');
}

static void test_redraw_flags(void)
{
    standin_reset();
    const int32_t unknown_flag = HOST_REDRAW_QUESTION << 1;
    screen_redraw(HOST_REDRAW_QUESTION);
    screen_redraw(HOST_REDRAW_NORMAL);
    screen_redraw(HOST_REDRAW_SOON | HOST_REDRAW_CLEAN);
    screen_redraw(unknown_flag);
    CHECK(standin()->redraw_flags == (HOST_REDRAW_QUESTION | HOST_REDRAW_SOON | HOST_REDRAW_CLEAN));
    standin_handlers = (standin_handlers_t){ .on_draw = on_draw };
    int32_t redraws = standin()->redraws;
    standin_draw();
    CHECK(standin()->redraws == redraws + 1);
    CHECK(standin()->redraw_flags == 0);
    screen_redraw(HOST_REDRAW_NORMAL);
    CHECK(standin()->redraw_flags == 0);
    screen_redraw(HOST_REDRAW_NO_BLINK);
    CHECK(standin()->redraw_flags == HOST_REDRAW_NO_BLINK);
    standin_terminate();
    standin_load();
    CHECK(standin()->redraw_flags == 0);
}

static void test_notification_validation(void)
{
    standin_reset();
    host_notice_t notice = { .icon = HOST_ICON_NONE };
    CHECK(notification_post(NULL) == HOST_ERR_ARGUMENT);
    CHECK(notification_post(&notice) == HOST_ERR_ARGUMENT);
    memcpy(notice.first, "Notice", sizeof "Notice");
    notice.icon = HOST_ICON_COUNT;
    CHECK(notification_post(&notice) == HOST_ERR_ARGUMENT);
    notice.icon = HOST_ICON_NONE - 1;
    CHECK(notification_post(&notice) == HOST_ERR_ARGUMENT);
    CHECK(standin()->notices == 0);
    notice.icon = HOST_ICON_NONE;
    CHECK(notification_post(&notice) == HOST_OK);
    CHECK(standin()->notices == 1);
    standin()->refuse_notices = HOST_ERR_NO_SPACE;
    CHECK(notification_post(&notice) == HOST_ERR_NO_SPACE);
    CHECK(standin()->notices == 1);
}

static void test_manual_poll_queue_and_cooldown(void)
{
    standin_reset();
    CHECK(!standin_poll_take(0));
    standin_settings();
    standin_intent(HOST_INTENT_NETWORK_UP);
    CHECK(!standin_poll_take(0));
    CHECK(poll_set_interval(0, 0) == HOST_ERR_ARGUMENT);
    CHECK(poll_set_interval(0, 60) == HOST_ERR_ARGUMENT);
    CHECK(poll_run(0) == HOST_OK);
    CHECK(standin_poll_take(0));
    CHECK(!standin_poll_take(0));
    CHECK(poll_run(0) == HOST_OK);
    CHECK(poll_run(0) == HOST_OK);
    CHECK(!standin_poll_take(0));
    standin_pass(1000);
    CHECK(standin_poll_take(0));
    CHECK(!standin_poll_take(0));
    const standin_pair_t date[] = { { "Retry-After", "Fri, 27 Nov 2026 12:02:00 GMT" } };
    standin()->now = 1795780800;
    standin_answer_headers(date, 1);
    standin_data(0, -1, 429, "", 0, 1, STANDIN_WHOLE, 0);
    CHECK(standin()->poll_cooldown_until_ms == standin()->uptime_ms + 120000);
    standin()->monitor_backoff = true;
    standin()->retry_until_ms = standin()->uptime_ms + 120000;
    CHECK(poll_run(0) == HOST_OK && standin()->early_requests == 0);
    CHECK(!standin_poll_take(0) && !standin_poll_take(1));
    standin_pass(119000);
    CHECK(!standin_poll_take(0));
    standin_pass(1000);
    CHECK(standin_poll_take(0) && standin_poll_take(1));
    CHECK(!standin_poll_take(0));
    standin_data(0, -1, HOST_FAILED_NO_NETWORK, "", 0, 1, STANDIN_WHOLE, 0);
    standin_intent(HOST_INTENT_NETWORK_UP);
    CHECK(standin_poll_take(0));
    standin_data(0, -1, 200, "", 0, 1, STANDIN_WHOLE, 0);
    standin_intent(HOST_INTENT_NETWORK_UP);
    CHECK(!standin_poll_take(0));
    standin_reset();
    standin_data(HOST_REQUEST_FIRST, -1, 429, "", 0, 1, STANDIN_WHOLE, 0);
    CHECK(standin()->poll_cooldown_until_ms == 0 && standin_poll_take(1));
    standin_data(1, -1, 429, "", 0, 1, STANDIN_WHOLE, 0);
    CHECK(standin()->poll_cooldown_until_ms == 60000);
}


static bool invert_on_load;
static int32_t blur_result, terminate_calls, exit_action;

static void load_inversion(void)
{
    if (invert_on_load) {
        CHECK(screen_set_inverted(1) == HOST_OK);
    }
}

static void save_on_blur(void)
{
    blur_result = session_set("old scene", 9);
}

static void terminate_count(void)
{
    terminate_calls++;
}

static void exit_on_button(int32_t event)
{
    button_count++;
    if (BTN_ACTION_OF(event) == exit_action) {
        screen_leave();
    }
}

static void exit_on_timer(int32_t id)
{
    (void)id;
    button_count++;
    screen_leave();
}

static void test_scene_exit_boundaries(void)
{
    const int32_t actions[] = { BTN_KEYDOWN, BTN_KEYUP, BTN_PRESSED, BTN_LONG_PRESSED };
    for (size_t index = 0; index < sizeof actions / sizeof actions[0]; index++) {
        standin_reset();
        standin_handlers = (standin_handlers_t){ .on_button = exit_on_button,
            .on_blur = save_on_blur, .on_terminate = terminate_count, .on_timer = exit_on_timer };
        standin_load(); standin_focus();
        CHECK(session_set("scene", 5) == HOST_OK);
        exit_action = actions[index]; button_count = terminate_calls = 0;
        if (exit_action == BTN_LONG_PRESSED) standin_long_press(BTN_UP);
        else standin_press(BTN_UP);
        CHECK(button_count == (exit_action == BTN_PRESSED ? 3 : exit_action == BTN_KEYDOWN ? 1 : 2));
        CHECK(standin()->is_leaving && !standin()->is_loaded);
        CHECK(standin()->session_length == 0 && !standin()->session_inverted);
        CHECK(blur_result == HOST_ERR_ARGUMENT && terminate_calls == 1);
        standin_timer(0); standin_press(BTN_OK); standin_draw();
        CHECK(terminate_calls == 1);
    }
    standin_reset();
    standin_handlers = (standin_handlers_t){ .on_timer = exit_on_timer, .on_blur = save_on_blur };
    standin_load(); standin_focus();
    standin_timer(0);
    CHECK(standin()->is_leaving && !standin()->is_loaded && blur_result == HOST_ERR_ARGUMENT);
}

static void test_load_time_inversion(void)
{
    standin_reset();
    standin_handlers = (standin_handlers_t){ .on_load = load_inversion };
    invert_on_load = true;
    standin_load(); standin_focus();
    CHECK(standin()->is_inverted && standin()->session_inverted);
    standin_leave();
    invert_on_load = false;
    standin_load(); standin_focus();
    CHECK(standin()->is_inverted && standin()->session_inverted);
    standin_leave(); standin_forget_session();
    standin_load(); standin_focus();
    CHECK(!standin()->is_inverted && !standin()->session_inverted);
}

static void test_question_press_boundaries(void)
{
    standin_reset();
    standin_handlers = (standin_handlers_t){ .on_button = on_button };
    standin_load(); standin_focus(); button_count = 0;
    screen_redraw(HOST_REDRAW_QUESTION);
    screen_redraw(HOST_REDRAW_NORMAL);
    standin_draw();
    CHECK(standin()->question != 0 && standin()->shown_question == standin()->question);
    standin_button_event(BTN_OK, BTN_KEYDOWN);
    CHECK(button_count == 0);
    standin_button_event(BTN_OK, BTN_KEYUP);
    standin_button_event(BTN_OK, BTN_PRESSED);
    CHECK(button_count == 2);
    button_count = 0;
    /* A fresh question is not shown yet when the next OK begins. */
    screen_redraw(HOST_REDRAW_QUESTION);
    standin_button_event(BTN_UP, BTN_PRESSED); /* handler boundary takes the question */
    button_count = 0;
    standin_button_event(BTN_OK, BTN_KEYDOWN);
    standin_draw();
    standin_button_event(BTN_OK, BTN_KEYUP);
    standin_button_event(BTN_OK, BTN_PRESSED);
    CHECK(button_count == 0);
    /* An OK that began on the previous question cannot answer its replacement. */
    standin_button_event(BTN_OK, BTN_KEYDOWN);
    screen_redraw(HOST_REDRAW_QUESTION);
    standin_draw();
    standin_button_event(BTN_OK, BTN_KEYUP);
    standin_button_event(BTN_OK, BTN_PRESSED);
    CHECK(button_count == 0);
    screen_redraw(HOST_REDRAW_NORMAL); standin_draw();
    CHECK(standin()->question == 0);
    standin_press(BTN_OK);
    CHECK(button_count == 3);
}

static void test_notification_retention(void)
{
    standin_reset();
    host_notice_t notice;
    memset(&notice, 'x', sizeof notice);
    notice.icon = HOST_ICON_NONE;
    /* A multibyte character cut by a full field is omitted. */
    notice.first[62] = '\xC2'; notice.first[63] = '\xB0';
    for (int index = 0; index < STANDIN_NOTICES; index++) CHECK(notification_post(&notice) == HOST_OK);
    CHECK(standin()->pending_notice_count == 4 && standin()->notices == 4);
    CHECK(strlen(standin()->last_notice.first) == 62);
    CHECK(strlen(standin()->last_notice.second) == 63);
    CHECK(strlen(standin()->last_notice.detail) == 95);
    CHECK(standin()->last_notice.ok[0] == 0 && standin()->last_notice.is_working == 0);
    CHECK(notification_post(&notice) == HOST_ERR_NO_SPACE);
    standin_notice_dismiss();
    CHECK(standin()->pending_notice_count == 3 && standin()->notices == 4);
    CHECK(notification_post(&notice) == HOST_OK);
    standin_restart();
    CHECK(standin()->pending_notice_count == 0);
}

static void test_effective_header(void)
{
    standin_reset();
    int32_t icon;
    if (standin_subject.is_fullscreen) {
        uint64_t frame = standin_screen(123);
        header_set_text(HOST_HEADER_LEFT, "Storage error.", 14);
        header_set_status(HOST_HEADER_RIGHT, HOST_ICON_CLOCK, "Storage error.", 14);
        CHECK_STR(standin_header_text(HOST_HEADER_LEFT, &icon), "");
        CHECK_STR(standin_header_text(HOST_HEADER_RIGHT, &icon), "");
        CHECK(standin_screen(123) == frame);
        return;
    }
    header_set_text(HOST_HEADER_LEFT, "Page", 4);
    header_set_status(HOST_HEADER_LEFT, HOST_ICON_CLOCK, "Hidden", 6);
    CHECK_STR(standin_header_text(HOST_HEADER_LEFT, &icon), "Page");
    CHECK(icon == HOST_ICON_NONE);
    uint64_t first = standin_screen(123);
    header_set_status(HOST_HEADER_LEFT, HOST_ICON_CLOCK, "Other hidden", 12);
    CHECK(standin_screen(123) == first);
    header_set_text(HOST_HEADER_LEFT, "", 0);
    CHECK_STR(standin_header_text(HOST_HEADER_LEFT, &icon), "Other hidden");
    CHECK(icon == HOST_ICON_CLOCK && standin_screen(123) != first);
    header_set_status(HOST_HEADER_RIGHT, HOST_ICON_CLOCK, "Status", 6);
    header_set_text(HOST_HEADER_RIGHT, "Hidden", 6);
    first = standin_screen(123);
    header_set_text(HOST_HEADER_RIGHT, "Other hidden", 12);
    CHECK_STR(standin_header_text(HOST_HEADER_RIGHT, &icon), "Status");
    CHECK(standin_screen(123) == first);
    header_set_status(HOST_HEADER_RIGHT, HOST_ICON_NONE, "", 0);
    CHECK_STR(standin_header_text(HOST_HEADER_RIGHT, &icon), "Other hidden");
}

static void test_geometry_and_chart_validation(void)
{
    standin_reset();
    host_list_t list = { .count = 100, .selected = 99 };
    int32_t height = standin_subject.is_fullscreen ? 300 : 260;
    host_cell_t cell = { .width = 400, .height = height, .value = "7" };
    CHECK(gfx_width() == 400 && gfx_height() == height);
    CHECK(ui_list_visible(&list) > 0 && ui_list_first(&list) > 0);
    CHECK(ui_cell_fit(&cell, NULL, HOST_ICON_NONE) == 1);
    app_list_t first_list = { .count = 100, .height = 92 };
    app_list_t second_list = { .count = 100, .height = 176 };
    CHECK(app_list_button(&first_list, BTN_DOWN_LONG_PRESSED) == APP_UI_CHANGED);
    CHECK(first_list.selected == ui_list_visible(&first_list));
    CHECK(app_list_button(&second_list, BTN_DOWN_LONG_PRESSED) == APP_UI_CHANGED);
    CHECK(second_list.selected == ui_list_visible(&second_list));
    CHECK(standin()->stray_draws == 0);
    standin()->is_drawing = true;
    const int32_t value = 1;
    host_chart_t chart = { .width = 100, .height = 50, .count = 1,
        .flags = HOST_CHART_FIXED_RANGE, .minimum = 0, .maximum = 10 };
    CHECK(ui_draw_chart(&chart, &value) == HOST_OK);
    chart.maximum = chart.minimum;
    CHECK(ui_draw_chart(&chart, &value) == HOST_ERR_ARGUMENT);
    chart.maximum = 10; chart.flags |= 64;
    CHECK(ui_draw_chart(&chart, &value) == HOST_ERR_ARGUMENT);
    CHECK(ui_draw_list_row(&list, INT32_MIN, "Invisible", 9, "", 0, HOST_ICON_NONE) == HOST_OK);
    CHECK(strstr(standin()->draw_text, "Invisible") == NULL);
}

static void test_storage_failures(void)
{
    standin_reset();
    struct { int32_t version, value; } state = { 1, 42 };
    CHECK(app_state_load("state", &state, sizeof state, 1) == HOST_ERR_NOT_FOUND);
    CHECK(app_state_save("state", &state, sizeof state));
    CHECK(app_state_load("state", &state, sizeof state, 2) == HOST_ERR_NOT_FOUND);
    CHECK(app_state_load("state", &state, sizeof state - 1, 1) == HOST_ERR_NOT_FOUND);
    CHECK(app_state_load("state", &state, 1, 1) == HOST_ERR_ARGUMENT);
    CHECK(app_state_load("state", &state, sizeof state, 1) == HOST_OK);
    standin()->refuse_reads = HOST_ERR_FAILED;
    CHECK(app_state_load("state", &state, sizeof state, 1) == HOST_ERR_FAILED);
    CHECK(kv_get("absent", 6, &state, sizeof state) == HOST_ERR_FAILED);
    CHECK(file_size("absent", 6) == HOST_ERR_FAILED);
    CHECK(kv_set("other", 5, "x", 1) == HOST_ERR_FAILED);
    CHECK(file_write("new.dat", 7, 0, "x", 1) == HOST_ERR_FAILED);
    standin()->refuse_reads = HOST_OK;
    CHECK(file_write("cache", 5, 0, "abcdef", 6) == HOST_OK);
    standin()->refuse_reads = HOST_ERR_FAILED;
    standin()->failed_read_bytes = 2;
    char read[6] = {0};
    CHECK(file_read("cache", 5, 0, read, sizeof read) == HOST_ERR_FAILED);
    CHECK(memcmp(read, "ab", 2) == 0 && read[2] == 0);
    standin()->is_drawing = true;
    CHECK(gfx_draw_bitmap_file(0, 0, 8, 1, "cache", 5, 0) == HOST_ERR_NOT_FOUND);
    standin()->is_drawing = false;
    standin()->refuse_reads = HOST_OK;
    standin()->refuse_writes = HOST_ERR_FAILED;
    standin()->write_failure_mode = STANDIN_WRITE_COMMIT;
    state.value = 77;
    CHECK(!app_state_save("state", &state, sizeof state));
    state.value = 0;
    CHECK(app_state_load("state", &state, sizeof state, 1) == HOST_OK && state.value == 77);
    CHECK(file_write("cache", 5, 0, "123456", 6) == HOST_ERR_FAILED);
    CHECK(memcmp(standin_file("cache", NULL), "123456", 6) == 0);
    standin()->write_failure_mode = STANDIN_WRITE_PARTIAL;
    CHECK(file_write("cache", 5, 0, "ABCDEF", 6) == HOST_ERR_FAILED);
    CHECK(memcmp(standin_file("cache", NULL), "ABC456", 6) == 0);
    CHECK(file_write("more", 4, 0, "ABCDEF", 6) == HOST_ERR_FAILED);
    CHECK(file_size("more", 4) == 3);
    CHECK(poll_set_interval(1, 60) == HOST_OK);
    standin()->refuse_intervals = HOST_ERR_FAILED;
    CHECK(poll_set_interval(1, 120) == HOST_ERR_FAILED);
    CHECK(standin()->poll_interval[1] == 60 && standin()->refused_intervals == 1);
    standin()->refuse_intervals = HOST_OK;
    CHECK(poll_set_interval(1, 120) == HOST_OK);
    standin()->refuse_requests = HOST_ERR_NO_SPACE;
    const char url[] = "https://example.test/value";
    CHECK(http_send(16, HOST_HTTP_GET, url, sizeof url - 1, NULL, 0, NULL, 0) == HOST_ERR_NO_SPACE);
    CHECK(standin()->request_count == 0 && standin()->refused_requests == 1);
    standin()->refuse_requests = HOST_OK;
    CHECK(http_send(16, HOST_HTTP_GET, url, sizeof url - 1, NULL, 0, NULL, 0) == HOST_OK);
    CHECK(standin()->request_count == 1);
}

static void test_shared_alarms(void)
{
    standin_reset();
    standin()->external_repeating_alarms = HOST_ALARM_REPEATING_MAX;
    CHECK(alarm_set(0, 60) == HOST_OK);
    CHECK(alarm_set_at(1, HOST_CLOCK_MIN) == HOST_OK);
    CHECK(alarm_set_every(2, 60, 0) == HOST_ERR_NO_SPACE);
    standin()->external_repeating_alarms--;
    CHECK(alarm_set_every(2, 60, 0) == HOST_OK);
    CHECK(alarm_set_every(2, 120, 0) == HOST_OK); /* no new reservation */
    CHECK(alarm_set_every(3, 60, 0) == HOST_ERR_NO_SPACE);
    CHECK(alarm_set(2, 0) == HOST_OK);
    CHECK(alarm_set_every(3, 60, 0) == HOST_OK);
}

static int installed_calls, updated_calls;
static void installed(void) { installed_calls++; }
static void replacement_load(void)
{
    CHECK(standin()->request_count == 0 && standin_alarms() == 0);
    CHECK(!standin()->poll_pending[0] && !standin()->poll_network_failed[0]);
    CHECK(standin()->poll_cooldown_until_ms == 0);
    CHECK(standin()->status_text[0][0] == 0 && standin()->session_length == 0);
    CHECK(alarm_set_every(0, 60, 0) == HOST_OK);
}
static void updated(void) { updated_calls++; CHECK(standin_alarms() == 1); }

static void test_replacement_and_markers(void)
{
    standin_reset();
    memset(&standin_handlers, 0, sizeof standin_handlers);
    standin_handlers.on_installed = installed;
    installed_calls = updated_calls = 0;
    standin()->refuse_lifecycle_reads = HOST_ERR_FAILED;
    standin_load(); CHECK(installed_calls == 0 && standin()->is_new);
    standin_terminate();
    standin()->refuse_lifecycle_reads = HOST_OK;
    standin()->refuse_lifecycle_writes = HOST_ERR_FAILED;
    standin_load(); CHECK(installed_calls == 0 && standin()->is_new);
    standin_terminate();
    standin()->refuse_lifecycle_writes = HOST_OK;
    standin_load(); CHECK(installed_calls == 1 && !standin()->is_new);
    standin_terminate();
    standin_load(); CHECK(installed_calls == 1);
    CHECK(kv_set("saved", 5, "value", 5) == HOST_OK);
    CHECK(file_write("saved", 5, 0, "value", 5) == HOST_OK);
    standin()->request_count = 1;
    standin()->poll_pending[0] = standin()->poll_network_failed[0] = true;
    standin()->poll_cooldown_until_ms = standin()->uptime_ms + 10000;
    standin()->session_length = 1;
    strcpy(standin()->status_text[0], "old");
    standin()->alarms[2].kind = STANDIN_ALARM_EVERY;
    CHECK(poll_set_interval(1, 120) == HOST_OK);
    standin_handlers.on_load = replacement_load;
    standin_handlers.on_updated = updated;
    standin_prepare_update();
    standin_load();
    CHECK(updated_calls == 1 && standin_alarms() == 1);
    CHECK(standin_value("saved", NULL) != NULL && standin_file("saved", NULL) != NULL);
    CHECK(standin()->poll_interval[1] == 120);
    memset(&standin_handlers, 0, sizeof standin_handlers);
}

static void test_declared_arena(void)
{
    standin_reset(); standin_load();
    CHECK(app_alloc(64) == app_alloc_arena);
    CHECK(app_alloc(1) == NULL);
    app_alloc_arena[63] = 17;
    standin_terminate(); standin_load();
    CHECK(app_alloc_left() == 64 && app_alloc_arena[63] == 0);
    CHECK(app_alloc(64) == app_alloc_arena);
}

int main(void)
{
    test_manual_poll_queue_and_cooldown();
    test_scene_exit_boundaries();
    test_load_time_inversion();
    test_question_press_boundaries();
    test_notification_retention();
    test_effective_header();
    test_geometry_and_chart_validation();
    test_storage_failures();
    test_shared_alarms();
    test_replacement_and_markers();
    test_declared_arena();
    test_package_files();
    test_time_text_zero_capacity();
    test_long_back();
    test_button_encoding();
    test_time_ranges();
    test_recurring_phase();
    test_log_sanitization();
    test_redraw_flags();
    test_notification_validation();
    REPORT();
}
