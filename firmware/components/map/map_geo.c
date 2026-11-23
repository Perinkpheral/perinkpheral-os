/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The map's projections in integers: angles as 2^-32 of a turn, unit vectors in Q30. The chip
 * has no FPU, and soft float would cost more than the land. Sines and cosines are series on an
 * eighth of a turn, arctangents CORDIC; both are good to about 1e-9, far below a pixel.
 */
#include "map_geo.h"

#include <stddef.h>

#define ONE            (1 << 30)
#define HALF_TURN      0x80000000u
#define QUARTER_TURN   0x40000000u
#define PI_HALF_Q30    1686629713      /* pi / 2 in Q30 */
#define CORDIC_GAIN_Q30 1768195363     /* the gain of the 30 steps below, 1.64676 */
#define CIRCUMFERENCE_M 40030230       /* 2 pi MAP_EARTH_RADIUS_M */
/* 2^62 / 2 R: half metres times it, shifted down 32, are radians in Q30. */
#define PER_DIAMETER_Q62 361927444964LL
#define FAR_SIDE_TURNS 1367130551u     /* 2 radians: past it an azimuthal point is no use */

/* atan(2^-i) in 2^-32 of a turn. */
static const uint32_t ATAN[30] = {
    536870912, 316933406, 167458907, 85004756, 42667331, 21354465, 10679838, 5340245,
    2670163,   1335087,   667544,    333772,   166886,   83443,    41722,    20861,
    10430,     5215,      2608,      1304,     652,      326,      163,      81,
    41,        20,        10,        5,        3,        1,
};

static int32_t mulq(int32_t a, int32_t b)
{
    return (int32_t)(((int64_t)a * b + (1 << 29)) >> 30);
}

/* sin and cos of x radians in Q30, 0 <= x <= pi/4, by their series. */
static void sincos_small(int32_t x, int32_t *sine, int32_t *cosine)
{
    static const int16_t SIN_STEPS[] = { 156, 110, 72, 42, 20, 6 };
    static const int16_t COS_STEPS[] = { 182, 132, 90, 56, 30, 12, 2 };
    int32_t x2 = mulq(x, x);
    int32_t value = ONE;
    for (size_t step = 0; step < sizeof SIN_STEPS / sizeof SIN_STEPS[0]; step++) {
        value = ONE - mulq(x2, value) / SIN_STEPS[step];
    }
    *sine = mulq(x, value);
    value = ONE;
    for (size_t step = 0; step < sizeof COS_STEPS / sizeof COS_STEPS[0]; step++) {
        value = ONE - mulq(x2, value) / COS_STEPS[step];
    }
    *cosine = value;
}

static void sincos_turn(uint32_t angle, int32_t *sine, int32_t *cosine)
{
    uint32_t quadrant = angle >> 30;
    uint32_t rest = angle & (QUARTER_TURN - 1);
    bool is_upper = rest > QUARTER_TURN / 2;
    if (is_upper) {
        rest = QUARTER_TURN - rest;
    }
    int32_t a, b;
    sincos_small((int32_t)(((int64_t)rest * PI_HALF_Q30) >> 30), &a, &b);
    if (is_upper) {
        int32_t swap = a;
        a = b;
        b = swap;
    }
    switch (quadrant) {
    case 0: *sine = a; *cosine = b; break;
    case 1: *sine = b; *cosine = -a; break;
    case 2: *sine = -a; *cosine = -b; break;
    default: *sine = -b; *cosine = a; break;
    }
}

/* Steps of CORDIC: 30 for a place asked about, 24 - well under a metre - for a pixel. */
#define STEPS_EXACT 30
#define STEPS_PIXEL 24

/* atan2(y, x) in 2^-32 of a turn. The vector is first brought to 2^28..2^29 for precision;
 * `length` is its length times the gain at that scale, `shift` how far it was moved left. */
static int32_t cordic(int64_t x, int64_t y, int steps, int32_t *length, int *shift)
{
    int64_t largest = x < 0 ? -x : x;
    int64_t other = y < 0 ? -y : y;
    if (other > largest) {
        largest = other;
    }
    *shift = 0;
    if (largest == 0) {
        *length = 0;
        return 0;
    }
    while (largest >= (1LL << 29)) {
        largest >>= 1;
        (*shift)--;
    }
    while (largest < (1LL << 28)) {
        largest <<= 1;
        (*shift)++;
    }
    int32_t vx = (int32_t)(*shift >= 0 ? x * (1LL << *shift) : x >> -*shift);
    int32_t vy = (int32_t)(*shift >= 0 ? y * (1LL << *shift) : y >> -*shift);
    uint32_t angle = 0;
    if (vx < 0) {
        vx = -vx;
        vy = -vy;
        angle = HALF_TURN;
    }
    for (int step = 0; step < steps; step++) {
        int32_t dx = vx >> step, dy = vy >> step;
        if (vy > 0) {
            vx += dy;
            vy -= dx;
            angle += ATAN[step];
        } else {
            vx -= dy;
            vy += dx;
            angle -= ATAN[step];
        }
    }
    *length = vx;
    return (int32_t)angle;
}

static int32_t turns_of(int32_t e6)
{
    int64_t scaled = (int64_t)e6 * (1LL << 32);
    return (int32_t)((scaled + (scaled < 0 ? -180000000 : 180000000)) / 360000000);
}

static void unit_vector(int32_t lat_e6, int32_t lon_e6, int32_t vector[3])
{
    int32_t sin_lat, cos_lat, sin_lon, cos_lon;
    sincos_turn((uint32_t)turns_of(lat_e6), &sin_lat, &cos_lat);
    sincos_turn((uint32_t)turns_of(lon_e6), &sin_lon, &cos_lon);
    vector[0] = mulq(cos_lat, cos_lon);
    vector[1] = mulq(cos_lat, sin_lon);
    vector[2] = sin_lat;
}

static void frame_at(int32_t lat_e6, int32_t lon_e6, map_frame_t *frame)
{
    int32_t sin_lat, cos_lat, sin_lon, cos_lon;
    sincos_turn((uint32_t)turns_of(lat_e6), &sin_lat, &cos_lat);
    sincos_turn((uint32_t)turns_of(lon_e6), &sin_lon, &cos_lon);
    frame->centre[0] = mulq(cos_lat, cos_lon);
    frame->centre[1] = mulq(cos_lat, sin_lon);
    frame->centre[2] = sin_lat;
    frame->east[0] = -sin_lon;
    frame->east[1] = cos_lon;
    frame->east[2] = 0;
    frame->north[0] = -mulq(sin_lat, cos_lon);
    frame->north[1] = -mulq(sin_lat, sin_lon);
    frame->north[2] = cos_lat;
}

void map_frame(const map_view_t *view, map_frame_t *frame)
{
    frame_at(view->lat_e6, view->lon_e6, frame);
}

static int32_t dot(const int32_t a[3], const int32_t b[3])
{
    return (int32_t)(((int64_t)a[0] * b[0] + (int64_t)a[1] * b[1] + (int64_t)a[2] * b[2] + (1 << 29)) >> 30);
}

/* Latitude and longitude of a vector, in 2^-32 of a turn. */
static void place_of(const int32_t p[3], int steps, int32_t *lat, int32_t *lon)
{
    int32_t length, other;
    int shift, unused;
    *lon = cordic(p[0], p[1], steps, &length, &shift);
    /* z at the scale and gain the length came out at; multiplied first, so it cannot overflow */
    int64_t z = ((int64_t)p[2] * CORDIC_GAIN_Q30) >> 30;
    z = shift >= 0 ? z * (1LL << shift) : z >> -shift;
    *lat = cordic(length, z, steps, &other, &unused);
}

#define CELLS_ACROSS (21600LL << MAP_CELL_FRACTION_BITS)

static int32_t column_of(int32_t lon)
{
    return (int32_t)(((uint64_t)((uint32_t)lon + HALF_TURN) * (uint64_t)CELLS_ACROSS) >> 32);
}

static int32_t row_of(int32_t lat)
{
    return (int32_t)(((uint64_t)((int64_t)QUARTER_TURN - lat) * (uint64_t)CELLS_ACROSS) >> 32);
}

int32_t map_row_cosine(int32_t row)
{
    int32_t sine, cosine;
    sincos_turn((uint32_t)(QUARTER_TURN - (uint32_t)(((uint64_t)(uint32_t)row << 32) / (uint64_t)CELLS_ACROSS)), &sine, &cosine);
    (void)sine;
    return cosine > 0 ? cosine : 0;
}

bool map_place_is_valid(int32_t lat_e6, int32_t lon_e6)
{
    return lat_e6 >= -90000000 && lat_e6 <= 90000000 && lon_e6 >= -180000000 && lon_e6 <= 180000000;
}

bool map_view_is_valid(const map_view_t *view)
{
    if (view->x < -MAP_COORDINATE_LIMIT || view->x > MAP_COORDINATE_LIMIT ||
        view->y < -MAP_COORDINATE_LIMIT || view->y > MAP_COORDINATE_LIMIT || view->w < 1 ||
        view->w > MAP_WIDTH_MAX || view->h < 1 || view->h > MAP_HEIGHT_MAX || view->land < 0 ||
        view->land > MAP_SHADE_MAX || view->sea < 0 || view->sea > MAP_SHADE_MAX ||
        (view->flags & ~MAP_COASTLINE) != 0) {
        return false;
    }
    if (view->projection == MAP_WORLD) {
        return view->lat_e6 == 0 && view->lon_e6 == 0 && view->m_per_px == 0;
    }
    if (view->projection != MAP_AEQD || !map_place_is_valid(view->lat_e6, view->lon_e6) ||
        view->m_per_px < MAP_SCALE_MIN || view->m_per_px > MAP_SCALE_MAX) {
        return false;
    }
    /* the corner: m * sqrt((w/2)^2 + (h/2)^2) <= MAP_REACH_MAX */
    int64_t diagonal = (int64_t)view->w * view->w + (int64_t)view->h * view->h;
    return (int64_t)view->m_per_px * view->m_per_px * diagonal <= 4LL * MAP_REACH_MAX * MAP_REACH_MAX;
}

void map_pixel_cell(const map_view_t *view, const map_frame_t *frame, int32_t px, int32_t py,
                    int32_t *row, int32_t *col)
{
    if (view->projection == MAP_WORLD) {
        *col = (int32_t)(((2LL * px + 1) * CELLS_ACROSS) / (2LL * view->w));
        *row = (int32_t)(((2LL * py + 1) * (CELLS_ACROSS / 2)) / (2LL * view->h));
        return;
    }
    MAP_TALLY(exact);
    /* the pixel's centre in half metres east and north of the map's */
    int64_t east = (int64_t)(2 * px + 1 - view->w) * view->m_per_px;
    int64_t north = (int64_t)(view->h - 2 * py - 1) * view->m_per_px;
    int32_t east_r = (int32_t)((east * PER_DIAMETER_Q62) >> 32);
    int32_t north_r = (int32_t)((north * PER_DIAMETER_Q62) >> 32);
    int32_t c2 = mulq(east_r, east_r) + mulq(north_r, north_r);
    /* cos c and sin(c)/c, c the angle from the centre, as series in c^2 */
    static const int16_t COS_STEPS[] = { 182, 132, 90, 56, 30, 12, 2 };
    static const int16_t SINC_STEPS[] = { 210, 156, 110, 72, 42, 20, 6 };
    int32_t cos_c = ONE, sinc_c = ONE;
    for (size_t step = 0; step < sizeof COS_STEPS / sizeof COS_STEPS[0]; step++) {
        cos_c = ONE - mulq(c2, cos_c) / COS_STEPS[step];
        sinc_c = ONE - mulq(c2, sinc_c) / SINC_STEPS[step];
    }
    int32_t p[3];
    for (int axis = 0; axis < 3; axis++) {
        p[axis] = mulq(cos_c, frame->centre[axis]) +
                  mulq(sinc_c, mulq(east_r, frame->east[axis]) + mulq(north_r, frame->north[axis]));
    }
    int32_t lat, lon;
    place_of(p, STEPS_PIXEL, &lat, &lon);
    *row = row_of(lat);
    *col = column_of(lon);
}

/* A vector on an azimuthal map, in pixels times 256 from the rectangle's corner. */
static bool project(const map_view_t *view, const map_frame_t *frame, const int32_t p[3],
                    int32_t *x256, int32_t *y256)
{
    int32_t length, unused;
    int shift, other;
    int32_t bearing = cordic(dot(p, frame->north), dot(p, frame->east), STEPS_EXACT, &length, &shift);
    int64_t toward = ((int64_t)dot(p, frame->centre) * CORDIC_GAIN_Q30) >> 30;
    toward = shift >= 0 ? toward * (1LL << shift) : toward >> -shift;
    uint32_t distance = (uint32_t)cordic(toward, length, STEPS_EXACT, &unused, &other);
    if (length == 0) {
        distance = toward >= 0 ? 0 : HALF_TURN;
    }
    if (distance > FAR_SIDE_TURNS) {
        return false;
    }
    int64_t metres256 = ((int64_t)distance * CIRCUMFERENCE_M) >> 24;
    int32_t sine, cosine;
    sincos_turn((uint32_t)bearing, &sine, &cosine);
    int64_t east256 = (metres256 * sine) >> 30;
    int64_t north256 = (metres256 * cosine) >> 30;
    *x256 = (int32_t)(view->w * 128 + east256 / view->m_per_px);
    *y256 = (int32_t)(view->h * 128 - north256 / view->m_per_px);
    return true;
}

static void world_place(const map_view_t *view, int32_t lat, int32_t lon, int32_t *x256, int32_t *y256)
{
    *x256 = (int32_t)(((uint64_t)((uint32_t)lon + HALF_TURN) * (uint64_t)(view->w * 256)) >> 32);
    *y256 = (int32_t)(((uint64_t)((int64_t)QUARTER_TURN - lat) * (uint64_t)(view->h * 512)) >> 32);
}

bool map_point(const map_view_t *view, int32_t lat_e6, int32_t lon_e6, int32_t *x, int32_t *y)
{
    int32_t px, py;
    if (view->projection == MAP_WORLD) {
        /* exact, and the edges clamp: 180 E is the last column, 90 S the last row */
        px = (int32_t)(((int64_t)lon_e6 + 180000000) * view->w / 360000000);
        py = (int32_t)(((int64_t)90000000 - lat_e6) * view->h / 180000000);
        px = px < view->w ? px : view->w - 1;
        py = py < view->h ? py : view->h - 1;
    } else {
        map_frame_t frame;
        int32_t p[3], x256, y256;
        map_frame(view, &frame);
        unit_vector(lat_e6, lon_e6, p);
        if (!project(view, &frame, p, &x256, &y256)) {
            *x = *y = 0;
            return false;
        }
        px = x256 >> 8;
        py = y256 >> 8;
    }
    if (px < 0 || py < 0 || px >= view->w || py >= view->h) {
        *x = *y = 0;
        return false;
    }
    *x = view->x + px;
    *y = view->y + py;
    return true;
}

bool map_ring_point(const map_view_t *view, const map_frame_t *frame, int32_t lat_e6,
                    int32_t lon_e6, int32_t radius_m, int index, int count, int32_t *x256,
                    int32_t *y256)
{
    map_frame_t place;
    frame_at(lat_e6, lon_e6, &place);
    int32_t sin_d, cos_d, sin_b, cos_b;
    sincos_turn((uint32_t)(((int64_t)radius_m << 32) / CIRCUMFERENCE_M), &sin_d, &cos_d);
    sincos_turn((uint32_t)(((uint64_t)index << 32) / (uint64_t)count), &sin_b, &cos_b);
    int32_t p[3];
    for (int axis = 0; axis < 3; axis++) {
        p[axis] = mulq(cos_d, place.centre[axis]) +
                  mulq(sin_d, mulq(cos_b, place.north[axis]) + mulq(sin_b, place.east[axis]));
    }
    if (view->projection == MAP_WORLD) {
        int32_t lat, lon;
        place_of(p, STEPS_EXACT, &lat, &lon);
        world_place(view, lat, lon, x256, y256);
        return true;
    }
    return project(view, frame, p, x256, y256);
}
