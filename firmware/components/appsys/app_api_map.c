/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Maps: the gfx_*map* calls, over components/map.
 */
#include "app_internal.h"
#include "map.h"

_Static_assert(HOST_MAP_WORLD == MAP_WORLD && HOST_MAP_AEQD == MAP_AEQD && HOST_MAP_COASTLINE == MAP_COASTLINE,
               "the contract names the map's projections and flags by the numbers map_geo.h gives them");
_Static_assert(sizeof(host_map_t) == sizeof(map_view_t), "host_map_t is map_view_t, field for field");

/* The map an application passes, copied out of its memory - which keeps no alignment the chip
 * would need - and held to what a map may be. */
static bool map_from(IM3Runtime runtime, void *memory, uint32_t offset, map_view_t *view)
{
    const uint8_t *bytes = app_memory_at(runtime, memory, offset, (int32_t)sizeof(host_map_t));
    if (bytes == NULL) {
        return false;
    }
    host_map_t map;
    memcpy(&map, bytes, sizeof map);
    *view = (map_view_t){ map.x, map.y, map.w, map.h, map.lat_e6, map.lon_e6, map.m_per_px,
                          map.projection, map.land, map.sea, map.flags };
    return map_view_is_valid(view);
}

/* A colour as gfx_set_color() takes it: greys become paper or ink without dithering. */
static int shade_of(const app_t *app, int32_t colour)
{
    bool is_grey = colour > HOST_PAPER && colour < HOST_INK;
    return is_grey && !app->services->dithers() ? (colour >= HOST_INK / 2 ? HOST_INK : HOST_PAPER) : colour;
}

/* The map's working memory comes from the application area on the device, where it counts
 * against the application; the computer has no area. */
static void *scratch_take(void)
{
#ifdef d_m3ApplicationArea
    return m3_area_take(MAP_SCRATCH_BYTES);
#else
    return malloc(MAP_SCRATCH_BYTES);
#endif
}

static void scratch_give(void *scratch)
{
#ifdef d_m3ApplicationArea
    m3_area_give(scratch);
#else
    free(scratch);
#endif
}

HOST_FUNCTION(gfx_draw_map)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, map_offset);

    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(app_not_drawing(app));
    }
    map_view_t view;
    if (!map_from(runtime, _mem, map_offset, &view)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    void *scratch = scratch_take();
    if (scratch == NULL) {
        m3ApiReturn(HOST_ERR_NO_SPACE);
    }
    map_draw(app->surface, &view, shade_of(app, view.land), shade_of(app, view.sea), scratch);
    scratch_give(scratch);
    m3ApiReturn(HOST_OK);
}

HOST_FUNCTION(gfx_map_point)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, map_offset);
    m3ApiGetArg(int32_t, lat_e6);
    m3ApiGetArg(int32_t, lon_e6);
    m3ApiGetArg(uint32_t, point_offset);

    map_view_t view;
    uint8_t *out = app_memory_to_fill(runtime, _mem, point_offset, (int32_t)sizeof(host_map_point_t));
    if (out == NULL || !map_from(runtime, _mem, map_offset, &view) || !map_place_is_valid(lat_e6, lon_e6)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    int32_t x, y;
    bool is_visible = map_point(&view, lat_e6, lon_e6, &x, &y);
    host_map_point_t point = { x, y, is_visible };
    memcpy(out, &point, sizeof point);
    m3ApiReturn(HOST_OK);
}

HOST_FUNCTION(gfx_draw_map_radius)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, map_offset);
    m3ApiGetArg(int32_t, lat_e6);
    m3ApiGetArg(int32_t, lon_e6);
    m3ApiGetArg(int32_t, radius_m);

    app_t *app = APP_OF(runtime);
    if (app->surface == NULL) {
        m3ApiReturn(app_not_drawing(app));
    }
    map_view_t view;
    if (!map_from(runtime, _mem, map_offset, &view) || !map_place_is_valid(lat_e6, lon_e6) ||
        radius_m < MAP_RADIUS_MIN || radius_m > MAP_RADIUS_MAX) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    map_draw_ring(app->surface, &view, lat_e6, lon_e6, radius_m);
    m3ApiReturn(HOST_OK);
}
