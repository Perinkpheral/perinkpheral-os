/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Optional helpers for application screens and nested views. Applications
 * may implement their own navigation and drawing.
 *
 *     static app_nav_t nav = { .screens = 4 };
 *
 *     void app_on_button(int32_t event)
 *     {
 *         if (app_nav_button(&nav, event) != APP_NAV_IGNORED) {
 *             screen_redraw(HOST_REDRAW_SOON);
 *             return;
 *         }
 *         ... what the screen or the view does with the press ...
 *     }
 *
 *     void app_on_back(void)
 *     {
 *         if (app_nav_button(&nav, BTN_BACK_PRESSED) == APP_NAV_IGNORED) { screen_leave(); }
 *         else { screen_redraw(HOST_REDRAW_SOON); }
 *     }
 *
 *     void app_on_draw(void)
 *     {
 *         int32_t top = app_nav_draw_title(&nav, title, true);
 *         ... the screen below `top` ...
 *     }
 *
 * Long UP and DOWN go to the previous and the next screen, round: only when there is more
 * than one screen, and only on a screen itself. Short presses, and long ones in an
 * application of one screen or in a view, are the application's. An application whose
 * short UP and DOWN have nothing else to do may switch screens with them too:
 * app_nav_step(). BACK closes the view on top; on a screen itself it is the application's,
 * which usually leaves.
 *
 * The screens need no names: a screen draws its own title. app_nav_t is a plain struct:
 * keep it in the session (app_session_save in app_on_blur) and give it to app_nav_check()
 * after loading it.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "host_api.h"

#define APP_NAV_VIEWS_MAX 4 /* views open over a screen at once */
#define APP_NAV_SCREENS_MAX 99 /* screens at most; more are counted as this many */
#define APP_NAV_ON_SCREEN (-1) /* app_nav_view() on the screen itself */

typedef struct {
    int32_t screens; /* how many; 0 and 1 are one screen */
    int32_t screen;  /* the one shown, from 0 */
    int32_t depth;   /* views open over it */
    int32_t views[APP_NAV_VIEWS_MAX]; /* the application's own ids; the top one is views[depth - 1] */
} app_nav_t;

typedef enum {
    APP_NAV_IGNORED, /* the press is the application's */
    APP_NAV_SCREEN,  /* another screen is shown: redraw */
    APP_NAV_CLOSED,  /* the view on top was closed: redraw */
} app_nav_result_t;

/* What a press does to the screens and views: long UP/DOWN and BACK as said above. */
app_nav_result_t app_nav_button(app_nav_t *nav, int32_t event);

/* The previous (by -1) or the next (by 1) screen, round, closing the views; for an
 * application that switches screens on short presses too. APP_NAV_IGNORED with one screen. */
app_nav_result_t app_nav_step(app_nav_t *nav, int32_t by);

/* Shows screen `screen`, closing the views over the one shown. */
void app_nav_go(app_nav_t *nav, int32_t screen);

/* Opens view `view` over what is shown; false when APP_NAV_VIEWS_MAX are open. */
bool app_nav_open(app_nav_t *nav, int32_t view);

/* The view on top, or APP_NAV_ON_SCREEN. */
int32_t app_nav_view(const app_nav_t *nav);

/* Makes `nav` sound after it was loaded from a session or the number of screens changed:
 * `screens` of them, the one shown and the views inside their bounds. */
void app_nav_check(app_nav_t *nav, int32_t screens);

/* Sets the left header to "2/4" followed by `left_text`, separated by a space.
 * NULL or "" supplies no app text; " " keeps the side empty without a count.
 * The count is omitted when hidden, on one screen or in a view. Text is capped at
 * HOST_HEADER_TEXT_MAX bytes between UTF-8 characters; Perinkpheral OS cuts it
 * with "..." to the header's available width, in its own font and size.
 * Call on focus (headers are cleared on leaving the screen), and when the count,
 * view or app text changes, before requesting a redraw. Use this instead of
 * header_set_text(HOST_HEADER_LEFT, ...) when the count belongs in the header.
 * Returns header_set_text()'s result. No drawing call; no retained state. */
int32_t app_nav_set_header(const app_nav_t *nav, const char *left_text, bool show_count);

/* The screen's title at the top left of the surface and, when `show_count` and there is
 * more than one screen, "2/4" at the right, small, on the title's baseline.
 * With NULL or "" nothing is drawn and 0 is returned: content starts at the top.
 * For these untitled screens, use app_nav_set_header() in the handlers above.
 * Otherwise returns the y below the title. */
int32_t app_nav_draw_title(const app_nav_t *nav, const char *title, bool show_count);
