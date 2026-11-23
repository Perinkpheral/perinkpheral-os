/* SPDX-License-Identifier: MIT */
/* The map service as an application sees it: the world and four regional maps, DOWN for the
 * next, each with its centre marked and a ring of the distance it shows. */
#include "host_api.h"

typedef struct {
    const char *name;
    int32_t     lat_e6, lon_e6, radius_m;
} place_t;

static const place_t PLACES[] = {
    { "World", 0, 0, 0 },
    { "Tokyo 2000 km", 35676200, 139650300, 2000000 },
    { "Istanbul 300 km", 41008200, 28978400, 300000 },
    { "Moscow 1000 km", 55755800, 37617300, 1000000 },
    { "Limassol 150 km", 34707100, 33022600, 150000 },
};
#define PLACE_COUNT ((int32_t)(sizeof PLACES / sizeof PLACES[0]))

static int32_t shown;

static int32_t length(const char *text)
{
    int32_t n = 0;
    while (text[n] != '\0') {
        n++;
    }
    return n;
}

void app_on_button(int32_t event)
{
    if (event == BTN_DOWN_PRESSED) {
        shown = (shown + 1) % PLACE_COUNT;
        screen_redraw(HOST_REDRAW_NORMAL);
    }
}

void app_on_draw(void)
{
    const place_t *place = &PLACES[shown];
    host_map_t map = { .w = gfx_width(), .h = gfx_height(), .land = 4, .sea = HOST_PAPER,
                       .flags = HOST_MAP_COASTLINE };
    if (place->radius_m > 0) {
        map.projection = HOST_MAP_AEQD;
        map.lat_e6 = place->lat_e6;
        map.lon_e6 = place->lon_e6;
        /* the ring a little inside the map's half height */
        map.m_per_px = place->radius_m / (map.h / 2 - 16);
    }
    gfx_draw_map(&map);
    if (place->radius_m > 0) {
        gfx_draw_map_radius(&map, place->lat_e6, place->lon_e6, place->radius_m);
    }
    /* Moscow on every map it is on, a cross */
    host_map_point_t point;
    if (gfx_map_point(&map, 55755800, 37617300, &point) == HOST_OK && point.visible) {
        gfx_draw_rect(point.x - 3, point.y, 7, 1, HOST_RECT_FILLED);
        gfx_draw_rect(point.x, point.y - 3, 1, 7, HOST_RECT_FILLED);
    }
    gfx_set_color(HOST_PAPER);
    gfx_draw_rect(0, 0, gfx_text_width(HOST_FONT_SMALL, place->name, length(place->name)) + 8, 17,
                  HOST_RECT_FILLED);
    gfx_set_color(HOST_INK);
    gfx_draw_text(4, 1, HOST_FONT_SMALL, place->name, length(place->name));
}
