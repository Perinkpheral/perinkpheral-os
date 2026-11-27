/* SPDX-License-Identifier: MIT */
#include "tinytest.h"
#include "standin.h"

static const char *const certificates[] = { "root.pem" };
const standin_subject_t standin_subject = {
    .writes_files = true,
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
    standin_handlers.on_button = on_button;
    button_count = 0;
    standin_long_press(BTN_BACK);
    CHECK(button_count == 1);
    CHECK(button_events[0] == BTN_BACK_KEYDOWN);
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
    standin_handlers.on_button = on_button;
    for (int32_t button = BTN_UP; button <= BTN_BACK; button++) {
        for (int32_t action = 0; action < 16; action++) {
            int32_t event = BTN_EVENT(button, action);
            CHECK(event == button * 16 + action);
            CHECK(BTN_OF(event) == button && BTN_ACTION_OF(event) == action);
        }
        button_count = 0;
        standin_press(button);
        CHECK(button_count == (button == BTN_BACK ? 2 : 3));
        for (int32_t action = 0; action < button_count; action++) {
            CHECK(button_events[action] == events[button - 1][action]);
        }
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
    const int64_t moments[] = { HOST_TIME_MIN, -1, 0, 946684800, HOST_CLOCK_MIN - 1,
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
    standin_handlers.on_draw = on_draw;
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

int main(void)
{
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
