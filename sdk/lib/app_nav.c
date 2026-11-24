/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_nav.h"
#include "app_text.h"

#include <stddef.h>

static int32_t count_of(const app_nav_t *nav)
{
    return nav->screens > APP_NAV_SCREENS_MAX ? APP_NAV_SCREENS_MAX : nav->screens > 1 ? nav->screens : 1;
}

app_nav_result_t app_nav_step(app_nav_t *nav, int32_t by)
{
    int32_t count = count_of(nav);
    if (count == 1) { return APP_NAV_IGNORED; }
    app_nav_go(nav, ((nav->screen + by) % count + count) % count);
    return APP_NAV_SCREEN;
}

app_nav_result_t app_nav_button(app_nav_t *nav, int32_t event)
{
    if (event == BTN_BACK_PRESSED && nav->depth > 0) {
        nav->depth--;
        return APP_NAV_CLOSED;
    }
    if (nav->depth == 0 && (event == BTN_UP_LONG_PRESSED || event == BTN_DOWN_LONG_PRESSED)) {
        return app_nav_step(nav, event == BTN_UP_LONG_PRESSED ? -1 : 1);
    }
    return APP_NAV_IGNORED;
}

void app_nav_go(app_nav_t *nav, int32_t screen)
{
    int32_t count = count_of(nav);
    nav->screen = screen < 0 ? 0 : screen >= count ? count - 1 : screen;
    nav->depth = 0;
}

bool app_nav_open(app_nav_t *nav, int32_t view)
{
    if (nav->depth < 0) { nav->depth = 0; }
    if (nav->depth >= APP_NAV_VIEWS_MAX) { return false; }
    nav->views[nav->depth++] = view;
    return true;
}

int32_t app_nav_view(const app_nav_t *nav)
{
    return nav->depth > 0 && nav->depth <= APP_NAV_VIEWS_MAX ? nav->views[nav->depth - 1] : APP_NAV_ON_SCREEN;
}

void app_nav_check(app_nav_t *nav, int32_t screens)
{
    nav->screens = screens;
    int32_t count = count_of(nav);
    /* The views were opened over the screen: a screen that is gone takes them with it. */
    if (nav->screen < 0 || nav->screen >= count) {
        nav->screen = 0;
        nav->depth = 0;
    }
    if (nav->depth < 0 || nav->depth > APP_NAV_VIEWS_MAX) { nav->depth = 0; }
}

/* Two digits on either side of the slash, at most. */
static int32_t count_text(const app_nav_t *nav, char *count)
{
    app_text_t text;
    app_text_start(&text, count, 6);
    app_text_add_number(&text, nav->screen + 1);
    app_text_add(&text, "/");
    app_text_add_number(&text, count_of(nav));
    return app_text_length(count);
}

int32_t app_nav_set_header(const app_nav_t *nav, const char *left_text, bool show_count)
{
    char line[HOST_HEADER_TEXT_MAX + 1];
    app_text_t text;
    app_text_start(&text, line, sizeof line);
    bool has_count = show_count && count_of(nav) > 1 && nav->depth == 0;
    if (has_count) {
        char count[6];
        count_text(nav, count);
        app_text_add(&text, count);
    }
    if (left_text != NULL && left_text[0] &&
        !(has_count && left_text[0] == ' ' && left_text[1] == '\0')) {
        if (has_count) { app_text_add(&text, " "); }
        app_text_add(&text, left_text);
    }
    return header_set_text(HOST_HEADER_LEFT, line, app_text_length(line));
}

int32_t app_nav_draw_title(const app_nav_t *nav, const char *title, bool show_count)
{
    if (title == NULL || !title[0]) { return 0; }
    bool has_count = show_count && count_of(nav) > 1;
    char count[6];
    int32_t length = has_count ? count_text(nav, count) : 0;
    int32_t count_width = has_count ? ui_text_width(HOST_UI_CAPTION, count, length) : 0;
    int32_t right = gfx_width() - HOST_UI_MARGIN;
    int32_t title_length = 0;
    while (title[title_length]) { title_length++; }
    int32_t room = right - HOST_UI_MARGIN - (has_count ? count_width + HOST_UI_MARGIN : 0);
    ui_draw_text(HOST_UI_MARGIN, HOST_UI_MARGIN, HOST_UI_TEXT, title, title_length, room);
    if (has_count) {
        /* On the title's baseline: the caption's line is lower, its top goes down by the difference. */
        int32_t y = HOST_UI_MARGIN + gfx_font_ascent(HOST_FONT_TEXT) - gfx_font_ascent(HOST_FONT_SMALL);
        ui_draw_text(right - count_width, y, HOST_UI_CAPTION, count, length, count_width);
    }
    return HOST_UI_MARGIN + ui_text_height(HOST_UI_TEXT);
}
