/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The kit: Perinkpheral OS's look for an application - lists, notices, text in
 * its styles, buttons, bars, cells, charts, pickers.
 */
#pragma once

#include "base.h"
#include "gfx.h"
#include "net.h" /* ui_draw_reason() draws HOST_FAILED_* */

/* ---- Kit: Perinkpheral OS interface parts ----
 *
 * The ui_* calls draw the parts that Perinkpheral OS screens are built from. The
 * drawing code is in Perinkpheral OS and is the same code that draws the menu
 * and Perinkpheral OS questions. Drawing calls require app_on_draw().
 * ui_list_visible(), ui_list_first(), ui_cell_fit(), ui_text_width(),
 * ui_text_height() and ui_value_width() work in any handler. */

/* List.
 *
 * One row per line. The selected row is inverted. A scroll bar is drawn
 * when the rows do not fit. `first` is the top row, kept by the
 * application: the rows move only when the selected one would leave the
 * screen, and then only as far as it needs. ui_list_first() says where
 * the top row is now; keep it in `first` (app_list_first() does).
 * The application keeps the list state and calls ui_draw_list_row() for the
 * visible rows:
 *
 *     static host_list_t list = { .count = 12 };
 *
 *     list.first = ui_list_first(&list);
 *     int32_t first = list.first;
 *     for (int32_t i = first; i < first + ui_list_visible(&list) && i < list.count; i++) {
 *         ui_draw_list_row(&list, i, label, label_length, value, value_length, HOST_ICON_NONE);
 *     }
 *
 * A list takes the full width of the surface, and `height` pixels from
 * `top` (0: to the bottom). ui_draw_title() returns a suitable `top`. For a
 * ui_draw_actions() row below the list, reduce the height by
 * HOST_UI_ACTIONS_HEIGHT.
 *
 * selected = -1: a list without a selection. UP and DOWN scroll it.
 * sdk/lib/app_ui.h handles the buttons for both kinds, a long UP or DOWN
 * a page at a time.
 *
 * Row layout: `value` is right-aligned; length 0 omits it. Label and
 * value never overlap. If both do not fit, a value up to half the row
 * width is kept whole and the label is cut with "..."; a longer value
 * gets the space the label leaves, at least half the row.
 * `icon` is a HOST_ICON_* drawn at the right edge, or HOST_ICON_NONE.
 * HOST_ICON_CHECK marks the active option. Rows outside the visible range
 * are not drawn. */
/* List count is at most 10000. ui_list_first() does not update first;
 * keep the returned value in the list, or use app_list_first(). */
typedef struct host_list {
    int32_t count;
    int32_t selected; /* or -1 */
    int32_t top;
    int32_t height;   /* 0: to the bottom of the surface */
    int32_t first;    /* the top row */
} host_list_t;
HOST_LAYOUT(host_list_t, 20);
HOST_IMPORT(ui_list_visible, int32_t ui_list_visible(const host_list_t *list));
HOST_IMPORT(ui_list_first, int32_t ui_list_first(const host_list_t *list));
HOST_IMPORT(ui_draw_list_row,
            int32_t ui_draw_list_row(const host_list_t *list, int32_t index, const char *label,
                                int32_t label_length, const char *value, int32_t value_length,
                                int32_t icon));

/* Notice: a full-surface message in the style of Perinkpheral OS.
 *
 * Layout: two lines of large text on the left, a large icon on the right,
 * button labels at the bottom. With both buttons present, "<ok> OK" is
 * left-aligned and "BACK <back>" is right-aligned. A single button is
 * centered. With `is_working`, three strokes replace the button labels.
 * All texts are null-terminated. `first` is required; other empty texts
 * are not drawn. The body uses the full height only when neither buttons
 * nor working strokes are present.
 * A destructive question must ignore presses made before it was shown on
 * the panel. */
typedef struct host_notice {
    char    first[64];  /* UTF-8: a field holds half as many two-byte characters */
    char    second[64];
    char    detail[96]; /* one or two lines of body text below the large lines */
    char    ok[32];     /* OK action: "Remove" */
    char    back[32];   /* BACK action: "Cancel" */
    int32_t icon;       /* HOST_ICON_*, or HOST_ICON_NONE */
    int32_t is_working;
} host_notice_t;
HOST_LAYOUT(host_notice_t, 296);
HOST_IMPORT(ui_draw_notice, int32_t ui_draw_notice(const host_notice_t *notice));

/* Text in a Perinkpheral OS style. `y` is the top of the line. The text is cut with
 * "..." at `max_width` pixels (0: no limit). Returns the width drawn.
 * Using the kit is optional; gfx_* calls can draw anything.
 * sdk/lib/app_ui.h implements button handling for the list, the picker
 * and the date editor. */
#define HOST_UI_TEXT    0 /* body text */
#define HOST_UI_TITLE   1 /* page title */
#define HOST_UI_CAPTION 2 /* caption next to a larger element */
#define HOST_UI_VALUE   3 /* the main value of a screen */
HOST_IMPORT(ui_draw_text, int32_t ui_draw_text(int32_t x, int32_t y, int32_t style, const char *text,
                                     int32_t length, int32_t max_width));

/* ui_draw_text_lines: text wrapped to `width`, preferring spaces and splitting
 * overlong words at UTF-8 character boundaries, at most `max_lines` lines
 * (1..HOST_UI_LINES_MAX). Input is cut to HOST_UI_TEXT_LINES_MAX bytes
 * between UTF-8 characters. The first NUL ends the text. The last line is cut with "..." if text
 * remains. Line height is ui_text_height() of the style. Returns the
 * number of lines drawn.
 *
 * ui_text_width, ui_text_height: text width and line height of a style.
 * Styles map to fonts: HOST_UI_TEXT = HOST_FONT_TEXT, HOST_UI_TITLE =
 * HOST_FONT_MEDIUM, HOST_UI_CAPTION = HOST_FONT_SMALL, HOST_UI_VALUE =
 * HOST_FONT_DISPLAY. The gfx_font_*() metrics of the font apply. */
#define HOST_UI_LINES_MAX 8
#define HOST_UI_TEXT_LINES_MAX 512
HOST_IMPORT(ui_draw_text_lines, int32_t ui_draw_text_lines(int32_t x, int32_t y, int32_t style,
                                                 const char *text, int32_t length, int32_t width,
                                                 int32_t max_lines));
HOST_IMPORT(ui_text_width, int32_t ui_text_width(int32_t style, const char *text,
                                                 int32_t length));
HOST_IMPORT(ui_text_height, int32_t ui_text_height(int32_t style));

/* Standard left and right margin of Perinkpheral OS screens. */
#define HOST_UI_MARGIN 16

/* Page title at the standard position, with a rule below it. Returns the
 * y of the content area. */
HOST_IMPORT(ui_draw_title, int32_t ui_draw_title(const char *text, int32_t length));

/* Built-in icon at a standard size. */
#define HOST_UI_ICON_SMALL  0 /* 22 px: next to text */
#define HOST_UI_ICON_MEDIUM 1 /* 44 px */
#define HOST_UI_ICON_LARGE  2 /* 99 px: notice icon */
HOST_IMPORT(ui_draw_icon, int32_t ui_draw_icon(int32_t x, int32_t y, int32_t icon, int32_t size));

/* One button label: name_length must be 1..8 bytes.
 * The button name ("UP") is white on black, and the action
 * text after it, or before it if `mirrored`. `x` is the left edge of the
 * whole label; `y` is the top of the text line. Returns the width. The
 * width cannot be measured without drawing; use ui_draw_actions() for
 * right-aligned labels. */
HOST_IMPORT(ui_draw_button, int32_t ui_draw_button(int32_t x, int32_t y, const char *name,
                                         int32_t name_length, const char *action,
                                         int32_t action_length, int32_t mirrored));

/* Button row at the bottom of the surface, HOST_UI_ACTIONS_HEIGHT pixels
 * high: the OK action at the left, the BACK action at the right. Length 0
 * omits a button. A single button is centred. */
#define HOST_UI_ACTIONS_HEIGHT 44
HOST_IMPORT(ui_draw_actions, int32_t ui_draw_actions(const char *ok, int32_t ok_length, const char *back,
                                           int32_t back_length));

/* Bar: a frame filled from the left in proportion `value` / `maximum`. */
HOST_IMPORT(ui_draw_bar, int32_t ui_draw_bar(int32_t x, int32_t y, int32_t width, int32_t value,
                                   int32_t maximum));

/* A value followed by its unit on the same baseline: "18 km/h",
 * "763 ppm", "25 min". The unit size is derived from the value size.
 * `y` is the top of the value line. `font` is a HOST_FONT_*, optionally
 * with HOST_FONT_TABULAR. Unit length 0 omits the unit.
 * Both calls return the total width. */
HOST_IMPORT(ui_draw_value, int32_t ui_draw_value(int32_t x, int32_t y, int32_t font, const char *value,
                                       int32_t value_length, const char *unit,
                                       int32_t unit_length));
HOST_IMPORT(ui_value_width, int32_t ui_value_width(int32_t font, const char *value,
                                                   int32_t value_length, const char *unit,
                                                   int32_t unit_length));

/* Draws the standard notice for a request status on the whole surface.
 *
 * status:  0 if no request was made yet, a HOST_FAILED_* code, or an HTTP
 *          status.
 * Notices: 0 "Loading"; HOST_FAILED_* one per code ("Not set up",
 *          "No network", "No answer" ...); 401, 403 "Access denied";
 *          404 "Not found"; any other non-2xx status "No answer" with
 *          the code.
 * Returns 1 if a notice was drawn. Returns 0 and draws nothing for a 2xx
 * status; the application reports missing data itself. */
HOST_IMPORT(ui_draw_reason, int32_t ui_draw_reason(int32_t status));

/* Frame around the value that UP and DOWN change, with an arrow above
 * and below for each available direction. */
#define HOST_UI_ARROW_UP   1
#define HOST_UI_ARROW_DOWN 2
HOST_IMPORT(ui_draw_frame, int32_t ui_draw_frame(int32_t x, int32_t y, int32_t width, int32_t height,
                                       int32_t arrows));

/* Cell: a name at the top, and below it a picture and a value side by
 * side, in a box of any size. Automatic sizing checks the measured value
 * width against the available width, without checking value height.
 * A cell covering the whole surface tries size classes 1..5; other cells
 * try 3..5. The smallest class is used if none fits.
 * Picture size: HOST_UI_CELL_PICTURE_LARGE pixels in a cell that covers
 * the whole surface, HOST_UI_CELL_PICTURE_SMALL otherwise. Pictures larger
 * than the square are downsampled to fit; smaller pictures use integer enlargement.
 * `picture` is a picture in gfx_draw_picture() format, or 0; `icon` is one of
 * Perinkpheral OS's icons (HOST_ICON_*) drawn in its place, or HOST_ICON_NONE.
 * Cells shown together should use one size: ui_cell_fit() returns the
 * size class: 1 HOST_FONT_HUGE, 2 HOST_FONT_DISPLAY, 3 HOST_FONT_LARGE,
 * 4 HOST_FONT_MEDIUM, or 5 HOST_FONT_TEXT. Set `size` of every related cell
 * to the largest returned class. size 0, or a value outside 1..5, selects
 * the size automatically. */
#define HOST_UI_CELL_PICTURE_LARGE 88
#define HOST_UI_CELL_PICTURE_SMALL 44
typedef struct host_cell {
    int32_t x, y, width, height;
    char    name[48];  /* empty: no name */
    char    value[48];
    int32_t size;
} host_cell_t;
HOST_LAYOUT(host_cell_t, 116);
HOST_IMPORT(ui_cell_fit, int32_t ui_cell_fit(const host_cell_t *cell, const uint8_t *picture,
                                             int32_t icon));
HOST_IMPORT(ui_draw_cell, int32_t ui_draw_cell(const host_cell_t *cell, const uint8_t *picture, int32_t icon));

/* Chart of a series of integers in a box. The vertical scale spans the
 * least and the greatest value of the series, unless HOST_CHART_FIXED_RANGE
 * selects `minimum` and `maximum`. The fixed range requires minimum < maximum;
 * it overrides HOST_CHART_FROM_ZERO. Values and marks are clipped to the box.
 * Fixed-range captions show minimum and maximum. Automatic captions show
 * the series extrema. Unknown flag bits are refused.
 * `values`: `count` numbers, 1 to HOST_CHART_VALUES_MAX, oldest first.
 * A value of HOST_CHART_NO_VALUE is a gap in the series.
 * decimals must be 0..9. width and height must be positive.
 * kind: HOST_CHART_LINE - points joined by a line; HOST_CHART_AREA - the
 * same with the area under the line in grey; HOST_CHART_BARS - one bar
 * per value; HOST_CHART_STEPS - a value holds until the next one.
 * flags: HOST_CHART_AXIS - a rule at the baseline; HOST_CHART_FROM_ZERO -
 * the scale includes 0; HOST_CHART_RANGE - the greatest and the least
 * value as captions at the left, printed with `decimals` decimals (215
 * with 1 is "21.5"); HOST_CHART_RANGE_RIGHT - the same captions at the
 * right, the series starting at the left edge; HOST_CHART_MARK_LAST - a
 * mark at the last value (LINE, AREA, STEPS).
 * Returns HOST_OK, HOST_ERR_ARGUMENT, or HOST_ERR_NOT_DRAWING outside
 * app_on_draw(). */
#define HOST_CHART_LINE  0
#define HOST_CHART_AREA  1
#define HOST_CHART_BARS  2
#define HOST_CHART_STEPS 3
#define HOST_CHART_AXIS      1
#define HOST_CHART_FROM_ZERO 2
#define HOST_CHART_RANGE     4
#define HOST_CHART_MARK_LAST 8
#define HOST_CHART_RANGE_RIGHT 16
#define HOST_CHART_FIXED_RANGE 32
#define HOST_CHART_VALUES_MAX 128
#define HOST_CHART_NO_VALUE   INT32_MIN
typedef struct host_chart {
    int32_t x, y, width, height;
    int32_t kind;
    int32_t flags;
    int32_t decimals;
    int32_t count;
    int32_t minimum, maximum; /* used only with HOST_CHART_FIXED_RANGE */
} host_chart_t;
HOST_LAYOUT(host_chart_t, 40);
HOST_IMPORT(ui_draw_chart, int32_t ui_draw_chart(const host_chart_t *chart, const int32_t *values));

/* Picker: one value of a sequence, on the whole surface. The selected
 * value is framed in the middle; the previous value is above it, the next
 * one below. An empty text marks the end of the sequence. The button row
 * is included: an empty `ok` or `back` is Perinkpheral OS's own word for it, in
 * the device's language - Save and Cancel. */
typedef struct host_picker {
    char above[64];
    char chosen[64];
    char below[64];
    char ok[32];
    char back[32];
} host_picker_t;
HOST_LAYOUT(host_picker_t, 256);
HOST_IMPORT(ui_draw_picker, int32_t ui_draw_picker(const host_picker_t *picker));

/* Date and time editor on the whole surface. The part being set is
 * framed. An empty `ok` or `back` is Perinkpheral OS's own word, in the device's
 * language: Next, or Save on the last part; Back, or Cancel on the first.
 * With nothing being set an empty one is no button.
 * All fields must be valid, including hidden parts: year 0..9999,
 * month 1..12, day 1..31, hour 0..23 and minute 0..59. has must include
 * HOST_DATE_HAS_TIME or HOST_DATE_HAS_DATE; selected must be
 * HOST_DATE_HOUR..HOST_DATE_NONE. */
#define HOST_DATE_HAS_TIME 1
#define HOST_DATE_HAS_DATE 2
#define HOST_DATE_HOUR     0
#define HOST_DATE_MINUTE   1
#define HOST_DATE_DAY      2
#define HOST_DATE_MONTH    3
#define HOST_DATE_YEAR     4
#define HOST_DATE_NONE     5 /* display only: no part is being set */
typedef struct host_date {
    int32_t year, month, day; /* month 1..12 */
    int32_t hour, minute;
    int32_t has;              /* HOST_DATE_HAS_* */
    int32_t selected;         /* HOST_DATE_HOUR .. HOST_DATE_NONE */
    char    ok[32];
    char    back[32];
} host_date_t;
HOST_LAYOUT(host_date_t, 92);
HOST_IMPORT(ui_draw_date, int32_t ui_draw_date(const host_date_t *date));
