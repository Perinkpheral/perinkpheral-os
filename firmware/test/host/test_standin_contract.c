/* SPDX-License-Identifier: MIT */
#include "tinytest.h"
#include "standin.h"

static const standin_poll_t polls[] = {
    { .url = "https://example.test/manual", .manual = true },
    { .url = "https://example.test/periodic" },
};
static const char *const certificates[] = { "root.pem" };
const standin_subject_t standin_subject = {
    .writes_files = true,
    .polls = polls,
    .poll_count = 2,
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

int main(void)
{
    test_manual_poll_queue_and_cooldown();
    test_package_files();
    test_long_back();
    test_log_sanitization();
    test_redraw_flags();
    test_notification_validation();
    REPORT();
}
