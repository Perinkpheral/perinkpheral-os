/* SPDX-License-Identifier: MIT */
#include "tinytest.h"
#include "standin.h"
#include "app_alloc.h"
#include "app_state.h"

APP_ALLOC_ARENA(64);
static const char *const hosts[] = { "example.test" };
static const standin_poll_t polls[] = { { .url = "https://example.test/value" } };

static const char *const certificates[] = { "root.pem" };
const standin_subject_t standin_subject = {
    .writes_files = true,
    .hosts = hosts, .host_count = 1,
    .polls = polls, .poll_count = 1,
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
    CHECK(poll_set_interval(0, 60) == HOST_OK);
    standin()->refuse_intervals = HOST_ERR_FAILED;
    CHECK(poll_set_interval(0, 120) == HOST_ERR_FAILED);
    CHECK(standin()->poll_interval[0] == 60 && standin()->refused_intervals == 1);
    standin()->refuse_intervals = HOST_OK;
    CHECK(poll_set_interval(0, 120) == HOST_OK);
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
    CHECK(alarm_set_at(1, HOST_TIME_MIN) == HOST_OK);
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
    standin()->session_length = 1;
    strcpy(standin()->status_text[0], "old");
    standin()->alarms[2].kind = STANDIN_ALARM_EVERY;
    CHECK(poll_set_interval(0, 120) == HOST_OK);
    standin_handlers.on_load = replacement_load;
    standin_handlers.on_updated = updated;
    standin_prepare_update();
    standin_load();
    CHECK(updated_calls == 1 && standin_alarms() == 1);
    CHECK(standin_value("saved", NULL) != NULL && standin_file("saved", NULL) != NULL);
    CHECK(standin()->poll_interval[0] == 120);
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
    test_storage_failures();
    test_shared_alarms();
    test_replacement_and_markers();
    test_declared_arena();
    test_package_files();
    test_long_back();
    test_log_sanitization();
    test_redraw_flags();
    test_notification_validation();
    REPORT();
}
