/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stddef.h>

#include "gfx.h"
#include "map_geo.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The map service's drawing: land and sea of a view into a surface, and rings on it. Land,
 * sea and coastline only - the data has no borders and no names. */

/* What map_draw() works in, lent by the caller for the call: a decoder, a 32-row band of the
 * projection's grid and two cursors a column. */
#define MAP_SCRATCH_BYTES 8128

/* Draws a valid view into its rectangle on the surface, clipped to the surface: every pixel
 * of it is land or sea in the shades given (0..16, the surface's ordered dither), with a solid
 * coastline when the view's flags ask for one. The surface's colour is left as it was. */
void map_draw(gfx_t *g, const map_view_t *view, int land, int sea, void *scratch);

/* The ring of places `radius_m` from a place, in the surface's colour, clipped to the view's
 * rectangle: 256 straight pieces, split where a world map's edges cut it. */
void map_draw_ring(gfx_t *g, const map_view_t *view, int32_t lat_e6, int32_t lon_e6,
                   int32_t radius_m);

#ifdef __cplusplus
}
#endif
