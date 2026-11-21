/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "notice.h"

#include <stddef.h>
#include <stdio.h>

#include "kit.h"

/* A notice of the host's own, on the whole screen: two lines on the left,
 * a large icon on the right, and along the bottom either what the buttons
 * do - each button's name white on black, what it does beside it - or
 * three strokes that stand for work going on. */
#define NOTICE_MARGIN          24
#define NOTICE_ICON_CELL       9  /* the icon is 99 pixels square */
#define NOTICE_ICON_FROM_EDGE   36 /* further in than the text is: it stands free */
#define NOTICE_FOOTER_HEIGHT   44
#define NOTICE_LINE_STEP       44 /* from one line's baseline to the next */
#define NOTICE_TEXT_ICON_GAP   16
#define NOTICE_STROKES         3
#define NOTICE_STROKE_GAP      24
#define NOTICE_STROKE_HEIGHT   6
#define NOTICE_TEXT_BUFFER     128
#define NOTICE_DETAIL_BUFFER   128
#define NOTICE_DETAIL_STEP     34 /* from the second line's baseline to the detail's */

void notice_draw(gfx_t *g, const notice_t *notice)
{
    /* With nothing along the bottom the notice has the whole surface. */
    bool has_footer = notice->is_working || notice->ok != NULL || notice->back != NULL;
    int  body_height = has_footer ? g->h - NOTICE_FOOTER_HEIGHT : g->h;
    int text_right = g->w - NOTICE_MARGIN;
    if (notice->icon != NOTICE_NO_ICON) {
        int icon_size = GFX_ICON_CELLS * NOTICE_ICON_CELL;
        int icon_left = g->w - NOTICE_ICON_FROM_EDGE - icon_size;
        /* Placed by what is seen: an icon's ink in the middle of its square
         * across, and in the middle of the text's height down - an icon
         * that does not fill its square, the signal's bars, would otherwise
         * stand high and in from the edge. */
        gfx_icon_ink_t ink;
        int            x = icon_left, y = (body_height - icon_size) / 2;
        if (gfx_icon_ink((gfx_icon_id_t)notice->icon, NOTICE_ICON_CELL, &ink)) {
            int ink_width = ink.last_x - ink.first_x + 1, ink_height = ink.last_y - ink.first_y + 1;
            x = icon_left + (icon_size - ink_width) / 2 - ink.first_x;
            y = (body_height - ink_height) / 2 - ink.first_y;
        }
        gfx_draw_icon(g, (gfx_icon_id_t)notice->icon, x, y, NOTICE_ICON_CELL, true);
        text_right = icon_left - NOTICE_TEXT_ICON_GAP;
    }
    const char *const lines[] = { notice->first, notice->second };
    int line_count = notice->second != NULL ? 2 : 1;
    int room = text_right - NOTICE_MARGIN;
    const gfx_font_t *font = gfx_font(GFX_FONT_MEDIUM);
    for (int line = 0; line < line_count; line++) {
        if (lines[line] != NULL && gfx_text_font_width(font, lines[line]) > room) {
            font = gfx_font_system();
        }
    }
    char detail[NOTICE_DETAIL_BUFFER];
    const char *detail_lines[] = { notice->detail, NULL };
    int detail_count = notice->detail != NULL ? 1 : 0;
    if (notice->detail != NULL && gfx_text_font_width(gfx_font_system(), notice->detail) > room) {
        snprintf(detail, sizeof detail, "%s", notice->detail);
        char *split = NULL;
        for (char *letter = detail; *letter; letter++) {
            if (*letter != ' ') continue;
            *letter = '\0';
            if (gfx_text_font_width(gfx_font_system(), detail) <= room) split = letter;
            *letter = ' ';
        }
        if (split != NULL) {
            *split = '\0';
            detail_lines[0] = detail;
            detail_lines[1] = split + 1;
            detail_count = 2;
        }
    }
    /* Keep the sentence and its explanation centered beside the icon. */
    int baseline = body_height / 2 - (line_count - 1) * NOTICE_LINE_STEP / 2 +
                   gfx_font_cap_height(font) / 2 - detail_count * NOTICE_DETAIL_STEP / 2;
    for (int line = 0; line < line_count; line++, baseline += NOTICE_LINE_STEP) {
        const char *text = lines[line] != NULL ? lines[line] : "";
        char fitted[NOTICE_TEXT_BUFFER];
        gfx_fit_text(font, text, room, fitted, sizeof fitted);
        gfx_text_font(g, NOTICE_MARGIN, baseline, font, fitted, true);
    }
    baseline -= NOTICE_LINE_STEP;
    for (int line = 0; line < detail_count; line++) {
        char fitted[NOTICE_DETAIL_BUFFER];
        gfx_fit_text(gfx_font_system(), detail_lines[line], room, fitted, sizeof fitted);
        baseline += NOTICE_DETAIL_STEP;
        gfx_text_font(g, NOTICE_MARGIN, baseline, gfx_font_system(), fitted, true);
    }

    if (notice->is_working) {
        int stroke_width =
            (g->w - 2 * NOTICE_MARGIN - (NOTICE_STROKES - 1) * NOTICE_STROKE_GAP) / NOTICE_STROKES;
        for (int stroke = 0; stroke < NOTICE_STROKES; stroke++) {
            gfx_fill(g, NOTICE_MARGIN + stroke * (stroke_width + NOTICE_STROKE_GAP),
                     g->h - NOTICE_FOOTER_HEIGHT / 2 - NOTICE_STROKE_HEIGHT / 2, stroke_width,
                     NOTICE_STROKE_HEIGHT, true);
        }
    } else {
        /* The row every screen has: OK at the left edge, BACK at the right. */
        kit_actions(g, notice->ok, notice->back);
    }
}
