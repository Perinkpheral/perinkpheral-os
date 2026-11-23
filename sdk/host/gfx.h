/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Graphics: the surface, text and fonts, shapes,
 * bitmaps, QR codes, Perinkpheral OS's icons, the application's pictures and maps.
 */
#pragma once

#include "base.h"

/* ---- Drawing and measurement ----
 *
 * Drawing calls require app_on_draw(). Text measurements, font metrics,
 * gfx_fit_text(), gfx_icon_ink() and gfx_map_point() work in any handler. gfx_draw_qr()
 * works in any handler when cell is 0. gfx_width() and gfx_height()
 * require app_on_draw(). */

/* Surface size: 400 x 300 for a full-screen application, 400 x 260 below
 * Perinkpheral OS header otherwise. The origin is the top left corner. The
 * default colour is black. Drawing is clipped to the surface. Drawing
 * coordinates are bounded to -4096..4096. gfx_draw_bitmap() and gfx_draw_bitmap_file()
 * require positive dimensions no larger than the surface, even when
 * clipping would hide the excess. QR cell size is 0..16. */
HOST_IMPORT(gfx_width, int32_t gfx_width(void));
HOST_IMPORT(gfx_height, int32_t gfx_height(void));

/* Text.
 *
 * All text in this API is UTF-8. Every font size contains printable ASCII.
 * Any other character is drawn with the first available option:
 *   1. its glyph, if the user has installed the glyph pack (Latin with
 *      diacritics, Cyrillic, Greek);
 *   2. an ASCII transliteration ("Moskva", "Strasse");
 *   3. '?'.
 * The degree sign ("\xC2\xB0") is built into every size.
 * Pass text unchanged, measure it, and cut it with gfx_fit_text(), not by
 * bytes.
 *
 * A font constant selects a size. The typeface is a user setting:
 * Departure Mono (monospaced) or Jersey (proportional). Character widths
 * differ, so layout must use measured widths: gfx_text_width(),
 * gfx_font_height(), gfx_font_ascent(), gfx_font_cap_height().
 *
 * Sizes in pixels, Departure Mono / Jersey:
 *                       capitals   line       characters per 400 px
 *   HOST_FONT_SMALL      8 / 10    15 / 21    57 / 50
 *   HOST_FONT_TEXT      16 / 15    30 / 28    28 / 33
 *   HOST_FONT_MEDIUM    24 / 25    45 / 43    19 / 21
 *   HOST_FONT_LARGE     32 / 30    60 / 56    14 / 16
 *   HOST_FONT_DISPLAY   48 / 50    90 / 86     9 / 10
 *   HOST_FONT_HUGE      80 / 80   150 / 140    5 / 6   */
#define HOST_FONT_TEXT    0 /* body text; Perinkpheral OS's own text */
#define HOST_FONT_MEDIUM  1
#define HOST_FONT_LARGE   2
#define HOST_FONT_DISPLAY 3
#define HOST_FONT_HUGE    4 /* five digits fill the width */
#define HOST_FONT_SMALL   5 /* units and captions */
/* Tabular digits: every digit has the width of the widest one. Use for
 * numbers that change in place, such as a clock or a countdown:
 *
 *     gfx_draw_text(x, y, HOST_FONT_HUGE | HOST_FONT_TABULAR, "12:07", 5);
 *
 * Every call that takes a font accepts the flag. Pass the same flag when
 * measuring. Characters other than digits keep their own width. */
#define HOST_FONT_TABULAR 0x100

/* Draws text. `y` is the top of the line, not the baseline. Text longer
 * than HOST_TEXT_MAX bytes is cut. Returns the width drawn. */
#define HOST_TEXT_MAX 127
HOST_IMPORT(gfx_draw_text, int32_t gfx_draw_text(int32_t x, int32_t y, int32_t font, const char *text,
                                       int32_t length));
HOST_IMPORT(gfx_text_width, int32_t gfx_text_width(int32_t font, const char *text,
                                                   int32_t length));

/* Where the ink of a text begins and ends, in pixels from its left edge as
 * gfx_draw_text() draws it: to centre or align a text by what is seen rather
 * than by its glyphs' room, which in some typefaces is more on one side. */
typedef struct host_text_ink {
    int32_t first_x, end_x; /* end_x: the first column after the ink; both 0 for none */
} host_text_ink_t;
HOST_LAYOUT(host_text_ink_t, 8);
HOST_IMPORT(gfx_text_ink, int32_t gfx_text_ink(int32_t font, const char *text, int32_t length,
                                               host_text_ink_t *ink));

/* Shortens `text` to `max_width` pixels. A text that does not fit is cut
 * between characters and "..." is appended. Writes the result to `fitted`
 * without a terminator. Returns its length; 0 if "..." alone does not fit.
 * HOST_ERR_NO_SPACE if capacity cannot hold the entire fitted result; no
 * bytes are copied on error. Capacity 0 succeeds only for an empty result. */
HOST_IMPORT(gfx_fit_text, int32_t gfx_fit_text(int32_t font, const char *text, int32_t length,
                                               int32_t max_width, char *fitted,
                                               int32_t capacity));

/* Font metrics, measured from the top of the line:
 *   gfx_font_height()      line height, including descenders;
 *   gfx_font_ascent()      distance to the baseline;
 *   gfx_font_cap_height()  height of capitals and digits above the baseline.
 * To place the top of the digits at `top`, draw the text at
 * top - (gfx_font_ascent() - gfx_font_cap_height()). */
HOST_IMPORT(gfx_font_height, int32_t gfx_font_height(int32_t font));
HOST_IMPORT(gfx_font_ascent, int32_t gfx_font_ascent(int32_t font));
HOST_IMPORT(gfx_font_cap_height, int32_t gfx_font_cap_height(int32_t font));

/* Sets the colour for the following calls: HOST_PAPER (0) to HOST_INK
 * (16). The colour is HOST_INK at the start of every app_on_draw().
 * Intermediate values are greys produced by dithering: N of every 16
 * pixels are inked, in a fixed 4 x 4 pattern. When the user disables
 * dithering, 1..7 become paper and 8..15 ink. Use greys for filled areas
 * only; thin lines and small text become unreadable. */
#define HOST_PAPER 0
#define HOST_INK   16
HOST_IMPORT(gfx_set_color, int32_t gfx_set_color(int32_t color));

#define HOST_RECT_OUTLINE 0
#define HOST_RECT_FILLED  1
HOST_IMPORT(gfx_draw_rect, int32_t gfx_draw_rect(int32_t x, int32_t y, int32_t width, int32_t height,
                                       int32_t fill));
HOST_IMPORT(gfx_draw_line, int32_t gfx_draw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1));

/* Draws a bitmap. `bits`: rows from top to bottom, the most significant
 * bit is the left pixel, each row padded to a whole byte. Set bits are
 * drawn in the current colour; clear bits are transparent. */
HOST_IMPORT(gfx_draw_bitmap, int32_t gfx_draw_bitmap(int32_t x, int32_t y, int32_t width, int32_t height,
                                           const uint8_t *bits));

/* Draws a QR code of `text`. (x, y) is the top left corner; `cell` is the
 * module size in pixels. The quiet zone of four modules is not drawn; the
 * application leaves it.
 * Returns the number of modules per side. With cell 0 nothing is drawn
 * and the call is valid in any handler.
 * Returns HOST_ERR_NO_SPACE if the text does not fit the largest code
 * (57 modules per side). */
#define HOST_QR_TEXT_MAX 200
HOST_IMPORT(gfx_draw_qr, int32_t gfx_draw_qr(int32_t x, int32_t y, int32_t cell, const char *text,
                                   int32_t length));

/* Draws a bitmap that Perinkpheral OS reads from an application file, starting
 * at `offset`. The format is that of gfx_draw_bitmap(). A file can hold
 * several bitmaps. The bitmap does not use application memory.
 * offset must be 0..65536. A failed or short read returns
 * HOST_ERR_NOT_FOUND and may leave part of the bitmap drawn. */
HOST_IMPORT(gfx_draw_bitmap_file,
            int32_t gfx_draw_bitmap_file(int32_t x, int32_t y, int32_t width, int32_t height,
                                    const char *name, int32_t name_length, int32_t offset));

/* Draws a built-in icon in the current colour. An icon is 11 x 11 cells;
 * (x, y) is its top left corner; `cell` is the cell size in pixels, 1 to
 * HOST_ICON_CELL_MAX. Cell size 2 matches Perinkpheral OS text size. Many are
 * available at finer resolutions of 22 and 44 cells. Perinkpheral OS
 * selects the available icon resolution for the requested size.
 * Returns HOST_ERR_ARGUMENT for an unknown icon. */
#define HOST_ICON_SIGNAL         0
#define HOST_ICON_NO_SIGNAL      1
#define HOST_ICON_UP             2
#define HOST_ICON_DOWN           3
#define HOST_ICON_LEFT           4
#define HOST_ICON_RIGHT          5
#define HOST_ICON_CHECK          6
#define HOST_ICON_CROSS          7
#define HOST_ICON_PLUS           8
#define HOST_ICON_MINUS          9
#define HOST_ICON_TRASH          10
#define HOST_ICON_GEAR           11
#define HOST_ICON_CLOCK          12
#define HOST_ICON_HOURGLASS      13
#define HOST_ICON_BELL           14
#define HOST_ICON_INFO           15
#define HOST_ICON_WARNING        16
#define HOST_ICON_PLAY           17
#define HOST_ICON_PAUSE          18
#define HOST_ICON_UPDATE         19
#define HOST_ICON_SUN            20
#define HOST_ICON_PARTLY_CLOUDY  21
#define HOST_ICON_CLOUD          22
#define HOST_ICON_RAIN           23
#define HOST_ICON_SNOW           24
#define HOST_ICON_THUNDER        25
#define HOST_ICON_WIND           26
#define HOST_ICON_UMBRELLA       27
#define HOST_ICON_DROP           28
#define HOST_ICON_THERMOMETER    29
#define HOST_ICON_BOLT           30
#define HOST_ICON_PLUG           31
#define HOST_ICON_BATTERY        32
#define HOST_ICON_HOME           33
#define HOST_ICON_BULB           34
#define HOST_ICON_DOOR           35
#define HOST_ICON_MOON           36
#define HOST_ICON_CHIP           37
#define HOST_ICON_NETWORK        38
#define HOST_ICON_GAUGE          39
#define HOST_ICON_CO2            40
#define HOST_ICON_SUNRISE        41
#define HOST_ICON_SUNSET         42
#define HOST_ICON_PARTICLES      43
#define HOST_ICON_POLLEN         44
#define HOST_ICON_GAS            45
#define HOST_ICON_DUST           46
#define HOST_ICON_LEAF           47
#define HOST_ICON_MASK           48
#define HOST_ICON_LAYERS         49
#define HOST_SINCE_HOST_ICON_LAYERS "0.1.84"
#define HOST_ICON_PROGRESS       50
#define HOST_SINCE_HOST_ICON_PROGRESS "0.1.84"
#define HOST_ICON_CUBE           51
#define HOST_SINCE_HOST_ICON_CUBE "0.1.84"
#define HOST_ICON_COUNT          52
#define HOST_ICON_NONE           (-1) /* where an icon is optional: none */
#define HOST_ICON_CELLS          11
#define HOST_ICON_CELL_MAX       16
HOST_IMPORT(gfx_draw_icon, int32_t gfx_draw_icon(int32_t x, int32_t y, int32_t icon, int32_t cell));

/* Where an icon drawn at `cell` has ink, in pixels from its top left
 * corner: an icon does not fill its square, and one that stands beside
 * text or at an edge is placed by what is seen. Draws nothing; any time.
 * first_x/first_y and last_x/last_y are inclusive ink coordinates.
 * Returns HOST_ERR_ARGUMENT for an unknown icon, leaving ink untouched. */
typedef struct host_icon_ink {
    int32_t first_x, last_x;
    int32_t first_y, last_y;
} host_icon_ink_t;
HOST_LAYOUT(host_icon_ink_t, 16);
HOST_IMPORT(gfx_icon_ink, int32_t gfx_icon_ink(int32_t icon, int32_t cell, host_icon_ink_t *ink));

/* Draws an application picture, as generated by tools/perinkpheral from an
 * image or from text rows of '#' and '.'.
 * Format: one byte width in cells, one byte height in cells, then one bit
 * per cell; 1 is drawn in the current colour, the leftmost cell is the
 * high bit, each row is padded to a whole byte.
 * `cell` is the cell size in pixels, 1 to HOST_ICON_CELL_MAX.
 * tools/perinkpheral generates pictures.h from the `pictures` folder of an
 * application. */
HOST_IMPORT(gfx_draw_picture,
            int32_t gfx_draw_picture(int32_t x, int32_t y, const uint8_t *picture, int32_t cell));

/* ---- Maps ----
 *
 * Land and sea of the whole Earth, drawn by Perinkpheral OS from Natural Earth's 1:10m land
 * (public domain) kept at one arc-minute: a cell is about 1.85 km north to south. Land, sea
 * and the coastline only: no borders, no countries, no cities, no names; the one lake is the
 * Caspian. Places are microdegrees: latitude -90000000..90000000, north positive; longitude
 * -180000000..180000000, east positive. Distances are along a sphere of radius 6371009 m.
 *
 * A map is a rectangle of the surface - x, y as other drawing (-4096..4096), w 1..400,
 * h 1..300 - and a projection:
 *   HOST_MAP_WORLD  the whole Earth over the rectangle: 180 W at the left edge, 90 N at the
 *                   top, degrees evenly spaced. lat_e6, lon_e6 and m_per_px must be 0.
 *   HOST_MAP_AEQD   azimuthal equidistant round (lat_e6, lon_e6): distance and direction
 *                   from the centre are true, north is up the centre's meridian (on a pole,
 *                   the centre's longitude runs down from the centre). m_per_px is
 *                   500..25000 metres a pixel, and no corner may be more than 6000 km from
 *                   the centre: m_per_px * sqrt((w/2)^2 + (h/2)^2) <= 6000000. A 400 x 260
 *                   map can take 25000; a 400 x 300 one 24000.
 * land and sea are colours as gfx_set_color() takes them, 0..16: dithered greys, or with
 * dithering disabled 1..7 paper and 8..15 ink. HOST_MAP_COASTLINE in flags draws a solid
 * line of ink on the land side of every coast; other bits must be 0. A pixel shows what is at
 * its centre. An invalid map is refused with HOST_ERR_ARGUMENT. */
#define HOST_MAP_WORLD 0
#define HOST_SINCE_HOST_MAP_WORLD "0.1.94"
#define HOST_MAP_AEQD 1
#define HOST_SINCE_HOST_MAP_AEQD "0.1.94"
#define HOST_MAP_COASTLINE 1
#define HOST_SINCE_HOST_MAP_COASTLINE "0.1.94"

typedef struct host_map {
    int32_t x, y, w, h;
    int32_t lat_e6, lon_e6, m_per_px;
    int32_t projection, land, sea, flags;
} host_map_t;
HOST_LAYOUT(host_map_t, 44);

typedef struct host_map_point {
    int32_t x, y, visible;
} host_map_point_t;
HOST_LAYOUT(host_map_point_t, 12);

/* Draws the map: every pixel of its rectangle, clipped to the surface, land or sea. The colour
 * stays as it was. Up to about a quarter of a second on the device, about a second for a
 * large map near a pole. While it draws it takes 8 KB of the application's 64 KB working
 * area; HOST_ERR_NO_SPACE when that much is not free. Requires app_on_draw(). */
#define HOST_SINCE_gfx_draw_map "0.1.94"
HOST_IMPORT(gfx_draw_map, int32_t gfx_draw_map(const host_map_t *map));

/* Where a place falls on the map: visible 1 and x, y of the pixel that holds it in surface
 * coordinates, ready for gfx_draw_*(); or visible 0 and x = y = 0 when that pixel is outside
 * the rectangle or the place is on the far side of an azimuthal map. A world map's edges
 * clamp: 180 E is its last column and 90 S its last row. Works in any handler. Returns
 * HOST_ERR_ARGUMENT, leaving point untouched, for an invalid map or place. */
#define HOST_SINCE_gfx_map_point "0.1.94"
HOST_IMPORT(gfx_map_point, int32_t gfx_map_point(const host_map_t *map, int32_t lat_e6,
                                                 int32_t lon_e6, host_map_point_t *point));

/* Draws the ring of places radius_m (100000..2000000) from a place, in the current colour,
 * clipped to the map's rectangle: 256 straight pieces, true distance on any projection. On an
 * azimuthal map centred on the place it is a circle of radius_m / m_per_px pixels; on a world
 * map a ring cut by the left or right edge goes on from the other. Requires app_on_draw(). */
#define HOST_SINCE_gfx_draw_map_radius "0.1.94"
HOST_IMPORT(gfx_draw_map_radius, int32_t gfx_draw_map_radius(const host_map_t *map, int32_t lat_e6,
                                                             int32_t lon_e6, int32_t radius_m));
