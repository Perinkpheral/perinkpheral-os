/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The map's geometry, integer only: which part of the Earth a rectangle of pixels shows, where
 * a place falls in it, and the ring of places at a distance from one. The sphere has the radius
 * MAP_EARTH_RADIUS_M; distances are along it. Pure: no drawing and no land, so the SDK's
 * stand-in host answers with the same numbers as the device. */

/* A count the benchmark keeps (firmware/test/host/bench_map.c); nothing otherwise. */
#ifndef MAP_TALLY
#define MAP_TALLY(what) ((void)0)
#endif

#define MAP_WORLD 0 /* equirectangular: the whole Earth stretched over the rectangle */
#define MAP_AEQD  1 /* azimuthal equidistant: true distance and direction from the centre */

#define MAP_COASTLINE 1 /* flags: a line of ink on the land side of every coast */

#define MAP_EARTH_RADIUS_M    6371009
#define MAP_WIDTH_MAX         400
#define MAP_HEIGHT_MAX        300
#define MAP_COORDINATE_LIMIT  4096
#define MAP_SCALE_MIN         500      /* metres a pixel */
#define MAP_SCALE_MAX         25000
#define MAP_REACH_MAX         6000000  /* metres from the centre to a corner: no antipode */
#define MAP_RADIUS_MIN        100000
#define MAP_RADIUS_MAX        2000000
#define MAP_SHADE_MAX         16

/* What the contract's host_map_t holds, field for field. */
typedef struct {
    int32_t x, y, w, h;
    int32_t lat_e6, lon_e6, m_per_px;
    int32_t projection, land, sea, flags;
} map_view_t;

bool map_view_is_valid(const map_view_t *view);
bool map_place_is_valid(int32_t lat_e6, int32_t lon_e6);

/* Where a place falls: the pixel of the rectangle that holds it, as surface coordinates.
 * False, with both 0, when that pixel is outside the rectangle. The view must be valid. */
bool map_point(const map_view_t *view, int32_t lat_e6, int32_t lon_e6, int32_t *x, int32_t *y);

/* ---- for the renderer ---- */

/* A view's orientation: the centre and the directions east and north at it, unit vectors in
 * Q30 (x towards 0 N 0 E, y towards 0 N 90 E, z north). */
typedef struct {
    int32_t centre[3], east[3], north[3];
} map_frame_t;

void map_frame(const map_view_t *view, map_frame_t *frame);

/* The source cell a pixel's centre falls in, in 1/512 of a cell: rows from 90 N, columns from
 * 180 W (MAP_LAND_WIDTH x MAP_LAND_HEIGHT cells of one arc-minute). The column is in
 * [0, 21600 * 512); the row in [0, 10800 * 512]. */
#define MAP_CELL_FRACTION_BITS 9
void map_pixel_cell(const map_view_t *view, const map_frame_t *frame, int32_t px, int32_t py,
                    int32_t *row, int32_t *col);

/* The cosine of a row's latitude in Q30, the row in 1/512 of a cell: how narrow its columns are. */
int32_t map_row_cosine(int32_t row);

/* A point of the ring of `radius_m` round a place, at `index` of `count` bearings from north,
 * in the rectangle's pixels times 256 (x right, y down; 0,0 the rectangle's corner). False
 * when the point is on the far side of an azimuthal map, where its place means nothing. */
bool map_ring_point(const map_view_t *view, const map_frame_t *frame, int32_t lat_e6,
                    int32_t lon_e6, int32_t radius_m, int index, int count, int32_t *x256,
                    int32_t *y256);

#ifdef __cplusplus
}
#endif
