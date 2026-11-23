/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_nav.h"

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

int32_t app_nav_draw_title(const app_nav_t *nav, const char *title, bool show_count)
{
    bool has_title = title != NULL && title[0];
    bool has_count = show_count && count_of(nav) > 1;
    if (!has_title && !has_count) { return 0; }
    /* "12/12" at most: two digits, a slash, two digits. */
    char count[6];
    int32_t length = 0;
    if (has_count) {
        int32_t shown = nav->screen + 1, total = count_of(nav);
        if (shown > 9) { count[length++] = (char)('0' + shown / 10 % 10); }
        count[length++] = (char)('0' + shown % 10);
        count[length++] = '/';
        if (total > 9) { count[length++] = (char)('0' + total / 10 % 10); }
        count[length++] = (char)('0' + total % 10);
    }
    int32_t count_width = has_count ? ui_text_width(HOST_UI_CAPTION, count, length) : 0;
    int32_t right = gfx_width() - HOST_UI_MARGIN;
    if (has_title) {
        int32_t title_length = 0;
        while (title[title_length]) { title_length++; }
        int32_t room = right - HOST_UI_MARGIN - (has_count ? count_width + HOST_UI_MARGIN : 0);
        ui_draw_text(HOST_UI_MARGIN, HOST_UI_MARGIN, HOST_UI_TEXT, title, title_length, room);
    }
    if (has_count) {
        /* On the title's baseline: the caption's line is lower, its top goes down by the difference. */
        int32_t y = HOST_UI_MARGIN + gfx_font_ascent(HOST_FONT_TEXT) - gfx_font_ascent(HOST_FONT_SMALL);
        ui_draw_text(right - count_width, y, HOST_UI_CAPTION, count, length, count_width);
    }
    return HOST_UI_MARGIN + ui_text_height(HOST_UI_TEXT);
}
