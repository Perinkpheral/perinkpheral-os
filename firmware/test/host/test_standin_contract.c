/* SPDX-License-Identifier: MIT */
#include "tinytest.h"
#include "standin.h"
#include "app_ui.h"

#ifndef STANDIN_FULLSCREEN
#define STANDIN_FULLSCREEN 0
#endif

static const char *const certificates[] = { "root.pem" };
const standin_subject_t standin_subject = {
    .writes_files = true,
    .is_fullscreen = STANDIN_FULLSCREEN,
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

int main(void)
{
    test_scene_exit_boundaries();
    test_load_time_inversion();
    test_question_press_boundaries();
    test_notification_retention();
    test_effective_header();
    test_geometry_and_chart_validation();
    test_package_files();
    test_long_back();
    test_log_sanitization();
    test_redraw_flags();
    test_notification_validation();
    REPORT();
}
