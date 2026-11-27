/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The map service: every row of the land decoded and held to what the generator encoded,
 * places that are land and sea, the integer projections against double ones at the poles, the
 * date line and the largest reach, points and their visibility, rings, clipping, and the views
 * the contract refuses. The renderer, which visits pixels in the land's order, is held to a
 * pixel-by-pixel lookup of every view.
 */
#include "map.h"
#include "map_land.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "tinytest.h"

#define SCREEN_W 400
#define SCREEN_H 300

static uint8_t bits[SCREEN_W / 8 * SCREEN_H];
static uint8_t scratch[MAP_SCRATCH_BYTES];
static gfx_t screen;

/* Every row of the land, decoded once: the reference lookups read it directly. */
static uint16_t *all_rows[MAP_LAND_HEIGHT];
static int all_counts[MAP_LAND_HEIGHT];

static void test_every_row(void)
{
    uint64_t hash = 0xcbf29ce484222325ull;
    int rows = 0, widest = 0;
    bool sound = true;
    for (int block = 0; block < MAP_LAND_BLOCKS; block++) {
        static map_land_t land;
        map_land_start(&land, block);
        while (map_land_next(&land)) {
            int count;
            const uint16_t *row = map_land_row(&land, &count);
            int values[1 + MAP_LAND_ROW_MAX];
            values[0] = count;
            for (int i = 0; i < count; i++) {
                values[1 + i] = row[i];
            }
            for (int i = 0; i <= count; i++) {
                hash = (hash ^ (uint8_t)values[i]) * 0x100000001b3ull;
                hash = (hash ^ (uint8_t)(values[i] >> 8)) * 0x100000001b3ull;
            }
            all_rows[land.row] = malloc(sizeof(uint16_t) * (size_t)(count > 0 ? count : 1));
            memcpy(all_rows[land.row], row, sizeof(uint16_t) * (size_t)count);
            all_counts[land.row] = count;
            widest = count > widest ? count : widest;
            rows++;
        }
        sound = sound && !land.is_broken;
    }
    CHECK(sound);
    CHECK(rows == MAP_LAND_HEIGHT);
    CHECK(widest == MAP_LAND_ROW_MAX);
    CHECK(hash == MAP_LAND_CHECK);
}

static bool land_at_cell(int row, int column)
{
    return map_land_at(all_rows[row], all_counts[row], column);
}

static bool land_of(double lat, double lon)
{
    int row = (int)floor((90 - lat) * 60), column = (int)floor((lon + 180) * 60);
    return land_at_cell(row < MAP_LAND_HEIGHT ? row : MAP_LAND_HEIGHT - 1, column % MAP_LAND_WIDTH);
}

static void test_places(void)
{
    CHECK(land_of(55.7558, 37.6173));   /* Moscow */
    CHECK(land_of(35.6762, 139.6503));  /* Tokyo */
    CHECK(land_of(-25.0, 135.0));       /* Australia */
    CHECK(land_of(72.0, -40.0));        /* Greenland */
    CHECK(land_of(-85.0, 0.0));         /* Antarctica */
    CHECK(land_of(-89.99, 100.0));      /* the south pole */
    CHECK(!land_of(30.0, -40.0));       /* the Atlantic */
    CHECK(!land_of(0.0, -150.0));       /* the Pacific */
    CHECK(!land_of(42.0, 51.0));        /* the Caspian, the source's one lake */
    CHECK(!land_of(89.99, 0.0));        /* the north pole */
    CHECK(!land_of(41.03, 29.01));      /* the Bosporus, a kilometre wide */
    CHECK(land_of(41.03, 29.06));       /* Asia beside it */
    CHECK(!land_of(0.0, 179.99) && !land_of(0.0, -179.99)); /* the date line */
}

/* ---- the reference: the same projections in double ---- */

#define R_EARTH 6371009.0
#define RAD     (M_PI / 180)

/* The cell under a point of the rectangle: u, v in pixels from its corner. */
static void reference_cell_at(const map_view_t *view, double u, double v, double *row, double *col)
{
    double lat, lon;
    if (view->projection == MAP_WORLD) {
        lon = u * 360 / view->w - 180;
        lat = 90 - v * 180 / view->h;
    } else {
        double lat0 = view->lat_e6 / 1e6 * RAD, lon0 = view->lon_e6 / 1e6 * RAD;
        double east = (u - view->w / 2.0) * view->m_per_px;
        double north = (view->h / 2.0 - v) * view->m_per_px;
        double rho = hypot(east, north), c = rho / R_EARTH;
        if (rho == 0) {
            lat = lat0 / RAD;
            lon = lon0 / RAD;
        } else {
            lat = asin(cos(c) * sin(lat0) + north * sin(c) * cos(lat0) / rho) / RAD;
            lon = (lon0 + atan2(east * sin(c), rho * cos(lat0) * cos(c) - north * sin(lat0) * sin(c))) / RAD;
        }
        lon = fmod(lon + 540, 360) - 180;
    }
    *row = (90 - lat) * 60;
    *col = (lon + 180) * 60;
}

static void reference_cell(const map_view_t *view, int px, int py, double *row, double *col)
{
    reference_cell_at(view, px + .5, py + .5, row, col);
}

static double column_gap(double a, double b)
{
    double gap = fabs(a - b);
    return gap > MAP_LAND_WIDTH / 2 ? MAP_LAND_WIDTH - gap : gap;
}

static map_view_t aeqd(double lat, double lon, int m)
{
    return (map_view_t){ .w = 400, .h = 260, .lat_e6 = (int32_t)lround(lat * 1e6),
                         .lon_e6 = (int32_t)lround(lon * 1e6), .m_per_px = m,
                         .projection = MAP_AEQD, .land = 16, .sea = 0 };
}

static const map_view_t WORLD = { .w = 400, .h = 260, .projection = MAP_WORLD, .land = 16 };

/* Views that test the edges: the poles, the date line, the largest reach, the smallest scale. */
static map_view_t views(int index)
{
    switch (index) {
    case 0: return WORLD;
    case 1: return aeqd(35.6762, 139.6503, 17544);  /* Tokyo, 2000 km */
    case 2: return aeqd(41.0082, 28.9784, 2632);    /* Istanbul, 300 km */
    case 3: return aeqd(55.7558, 37.6173, 8772);    /* Moscow, 1000 km */
    case 4: return aeqd(34.7071, 33.0226, 1316);    /* Limassol, 150 km */
    case 5: return aeqd(90, 0, 25000);              /* the north pole */
    case 6: return aeqd(89.9, 45, 25000);
    case 7: return aeqd(78, 15, 25000);             /* Svalbard: the pole inside */
    case 8: return aeqd(-90, 0, 25000);             /* the south pole */
    case 9: return aeqd(-70, -60, 20000);
    case 10: return aeqd(0, 180, 25000);            /* the date line */
    case 11: return aeqd(-17, -179.9, 500);         /* Fiji, the date line close up */
    case 12: return aeqd(60, -150, 25000);
    default: return (map_view_t){ 0 };
    }
}
#define VIEW_COUNT 13

static void test_projection(void)
{
    for (int index = 0; index < VIEW_COUNT; index++) {
        map_view_t view = views(index);
        CHECK(map_view_is_valid(&view));
        map_frame_t frame;
        map_frame(&view, &frame);
        double worst = 0;
        for (int py = 0; py < view.h; py += 3) {
            for (int px = 0; px < view.w; px += 3) {
                int32_t row, col;
                double expected_row, expected_col;
                map_pixel_cell(&view, &frame, px, py, &row, &col);
                reference_cell(&view, px, py, &expected_row, &expected_col);
                double off_row = fabs(row / 512.0 - expected_row);
                /* a column near a pole is narrow: count it by the ground it covers */
                double off_col = column_gap(col / 512.0, expected_col) * cos((90 - expected_row / 60) * RAD);
                worst = fmax(worst, fmax(off_row, off_col));
            }
        }
        /* the 1/512 of a cell it is kept in, under four metres */
        if (worst > 1.0 / 256) {
            printf("view %d: off by %.5f cells\n", index, worst);
        }
        CHECK(worst <= 1.0 / 256);
    }
    /* the world: exact, both ends */
    map_frame_t frame = { 0 };
    int32_t row, col;
    map_pixel_cell(&WORLD, &frame, 0, 0, &row, &col);
    CHECK(row == (int32_t)((10800LL * 512) / 520) && col == (int32_t)((21600LL * 512) / 800));
    map_pixel_cell(&WORLD, &frame, 399, 259, &row, &col);
    CHECK((row >> 9) == 10779 && (col >> 9) == 21573);
}

/* Where the reference puts a place on a view. */
static bool reference_point(const map_view_t *view, double lat, double lon, int *x, int *y)
{
    double u, v;
    if (view->projection == MAP_WORLD) {
        u = (lon + 180) * view->w / 360;
        v = (90 - lat) * view->h / 180;
        u = fmin(u, view->w - 1);
        v = fmin(v, view->h - 1);
    } else {
        double lat0 = view->lat_e6 / 1e6 * RAD, lon0 = view->lon_e6 / 1e6 * RAD;
        double phi = lat * RAD, dlon = lon * RAD - lon0;
        double cos_c = sin(lat0) * sin(phi) + cos(lat0) * cos(phi) * cos(dlon);
        double c = acos(fmax(-1, fmin(1, cos_c)));
        double k = c == 0 ? 1 : c / sin(c);
        double east = R_EARTH * k * cos(phi) * sin(dlon);
        double north = R_EARTH * k * (cos(lat0) * sin(phi) - sin(lat0) * cos(phi) * cos(dlon));
        u = view->w / 2.0 + east / view->m_per_px;
        v = view->h / 2.0 - north / view->m_per_px;
        /* a place on the centre comes out a hair to one side of it */
        u = round(u * 1e6) / 1e6;
        v = round(v * 1e6) / 1e6;
    }
    *x = (int)floor(u);
    *y = (int)floor(v);
    return *x >= 0 && *y >= 0 && *x < view->w && *y < view->h;
}

static void test_points(void)
{
    /* a grid of places on every view: the same pixel as the reference, or one beside it when
     * the place is on a pixel's edge */
    for (int index = 0; index < VIEW_COUNT; index++) {
        map_view_t view = views(index);
        view.x = 7;
        view.y = -3;
        int agreed = 0, near = 0, total = 0;
        for (int lat = -90; lat <= 90; lat += 3) {
            for (int lon = -180; lon <= 180; lon += 3) {
                double la = lat + (index % 2) * 0.37, lo = lon + 0.11;
                if (la > 90 || lo > 180) {
                    continue;
                }
                int32_t x = 99, y = 99;
                int ex, ey;
                bool visible = map_point(&view, (int32_t)lround(la * 1e6), (int32_t)lround(lo * 1e6), &x, &y);
                bool expected = reference_point(&view, la, lo, &ex, &ey);
                if (!visible) {
                    CHECK(x == 0 && y == 0);
                }
                if (visible != expected) {
                    /* only at an edge of the rectangle */
                    CHECK(ex >= -1 && ey >= -1 && ex <= view.w && ey <= view.h);
                    continue;
                }
                if (!visible) {
                    continue;
                }
                total++;
                if (x - view.x == ex && y - view.y == ey) {
                    agreed++;
                } else if (abs(x - view.x - ex) <= 1 && abs(y - view.y - ey) <= 1) {
                    near++;
                }
            }
        }
        CHECK(agreed + near == total);
        if (near * 100 > total) {
            printf("view %d: %d of %d points a pixel off\n", index, near, total);
        }
        CHECK(near * 100 <= total);
    }
    /* the centre, the far side and the edges */
    map_view_t view = aeqd(0, 0, 25000);
    int32_t x, y;
    CHECK(map_point(&view, 0, 0, &x, &y) && x == 200 && y == 130);
    CHECK(!map_point(&view, 0, 180000000, &x, &y) && x == 0 && y == 0);   /* the antipode */
    CHECK(!map_point(&view, 0, -180000000, &x, &y) && x == 0 && y == 0);
    CHECK(!map_point(&view, 60000000, 0, &x, &y));                         /* 6700 km: off the map */
    CHECK(map_point(&view, 1000000, 0, &x, &y) && x == 200 && y == 125); /* 111 km north: 4.45 px up */
    CHECK(map_point(&WORLD, 90000000, -180000000, &x, &y) && x == 0 && y == 0);
    CHECK(map_point(&WORLD, -90000000, 180000000, &x, &y) && x == 399 && y == 259);
    CHECK(map_point(&WORLD, 0, 0, &x, &y) && x == 200 && y == 130);
    map_view_t pole = aeqd(90, 0, 25000);
    CHECK(map_point(&pole, 90000000, 123000000, &x, &y) && x == 200 && y == 130);
    /* on a polar map the longitude of the centre is down the middle */
    CHECK(map_point(&pole, 80000000, 0, &x, &y) && x == 200 && y > 130);
    CHECK(map_point(&pole, 80000000, 90000000, &x, &y) && x > 200 && y == 130);
}

static void test_validation(void)
{
    map_view_t good = aeqd(10, 20, 25000);
    CHECK(map_view_is_valid(&good));
    map_view_t view;
#define REFUSED(field, value)              \
    view = good;                           \
    view.field = value;                    \
    CHECK(!map_view_is_valid(&view));
    REFUSED(w, 0);
    REFUSED(w, 401);
    REFUSED(h, 0);
    REFUSED(h, 301);
    REFUSED(x, 4097);
    REFUSED(y, -4097);
    REFUSED(land, 17);
    REFUSED(sea, -1);
    REFUSED(flags, 2);
    REFUSED(projection, 2);
    REFUSED(projection, -1);
    REFUSED(m_per_px, 499);
    REFUSED(m_per_px, 25001);
    REFUSED(lat_e6, 90000001);
    REFUSED(lon_e6, -180000001);
    /* the corner past 6000 km: a full screen at the largest scale */
    REFUSED(h, 300);
    view = good;
    view.h = 300;
    view.m_per_px = 24000;
    CHECK(map_view_is_valid(&view));
    view = WORLD;
    CHECK(map_view_is_valid(&view));
    view.m_per_px = 1;
    CHECK(!map_view_is_valid(&view));
    view = WORLD;
    view.lat_e6 = 1;
    CHECK(!map_view_is_valid(&view));
    view = WORLD;
    view.w = 400;
    view.h = 300;
    view.flags = MAP_COASTLINE;
    CHECK(map_view_is_valid(&view));
    CHECK(map_place_is_valid(-90000000, 180000000));
    CHECK(!map_place_is_valid(0, 180000001));
#undef REFUSED
}

/* ---- drawing ---- */

static void fresh(uint8_t fill)
{
    memset(bits, fill, sizeof bits);
    gfx_init(&screen, bits, SCREEN_W, SCREEN_H);
}

/* True when some cell within `pixels` of ground from the pixel's exact place holds `land`. */
static bool land_near(const map_view_t *view, int px, int py, double pixels, bool land)
{
    double row, col;
    reference_cell(view, px, py, &row, &col);
    double reach_rows = view->projection == MAP_WORLD ? pixels * MAP_LAND_HEIGHT / view->h
                                                      : pixels * view->m_per_px / 1853.25;
    double reach_cols = view->projection == MAP_WORLD ? pixels * MAP_LAND_WIDTH / view->w
                                                      : reach_rows / fmax(cos((90 - row / 60) * RAD), 1e-3);
    for (int r = (int)floor(row - reach_rows); r <= (int)floor(row + reach_rows); r++) {
        for (int c = (int)floor(col - reach_cols); c <= (int)floor(col + reach_cols); c++) {
            int rr = r < 0 ? 0 : r >= MAP_LAND_HEIGHT ? MAP_LAND_HEIGHT - 1 : r;
            if (land_at_cell(rr, (c % MAP_LAND_WIDTH + MAP_LAND_WIDTH) % MAP_LAND_WIDTH) == land) {
                return true;
            }
        }
    }
    return false;
}

/* The renderer against a pixel-by-pixel lookup through the exact projection. Interpolation
 * moves a pixel's centre by under a tenth of a pixel, so where they differ, land within a tenth
 * of a pixel of the centre must be what was drawn. */
static void test_render(void)
{
    for (int index = 0; index < VIEW_COUNT; index++) {
        map_view_t view = views(index);
        fresh(0xA5);
        map_draw(&screen, &view, 16, 0, scratch);
        map_frame_t frame;
        map_frame(&view, &frame);
        int differ = 0, land = 0, unexplained = 0;
        for (int py = 0; py < view.h; py++) {
            for (int px = 0; px < view.w; px++) {
                int32_t row, col;
                map_pixel_cell(&view, &frame, px, py, &row, &col);
                row >>= MAP_CELL_FRACTION_BITS;
                bool expected = land_at_cell(row < MAP_LAND_HEIGHT ? row : MAP_LAND_HEIGHT - 1, col >> MAP_CELL_FRACTION_BITS);
                bool drawn = gfx_get(&screen, px, py);
                land += drawn;
                if (drawn == expected) {
                    continue;
                }
                differ++;
                bool near = land_near(&view, px, py, 0.1, drawn);
                unexplained += !near;
                if (!near && getenv("MAP_DEBUG")) {
                    double rr, cc;
                    reference_cell(&view, px, py, &rr, &cc);
                    printf("  view %d px %d py %d drawn %d exact row %d col %d ref %.3f %.3f\n", index, px, py, drawn,
                           row >> MAP_CELL_FRACTION_BITS, col >> MAP_CELL_FRACTION_BITS, rr, cc);
                }
            }
        }
        if (unexplained > 0 || differ * 200 > view.w * view.h) {
            printf("view %d: %d of %d pixels differ, %d not near\n", index, differ, view.w * view.h, unexplained);
        }
        CHECK(unexplained == 0);
        CHECK(differ * 200 <= view.w * view.h); /* under 0.5 % */
        if (index == 0) {
            CHECK(differ == 0);
            CHECK(land > 20000 && land < 40000);
        }
        /* below the rectangle nothing changed */
        bool untouched = true;
        for (int py = view.h; py < SCREEN_H; py++) {
            for (int px = 0; px < SCREEN_W; px++) {
                untouched = untouched && gfx_get(&screen, px, py) == !((0xA5 << (px & 7)) & 0x80);
            }
        }
        CHECK(untouched);
    }
}

static void test_render_clipped(void)
{
    static uint8_t whole[sizeof bits];
    map_view_t view = aeqd(41.0082, 28.9784, 2632);
    view.flags = MAP_COASTLINE;
    view.land = 4;
    view.x = 30;
    view.y = 20;
    fresh(0xFF);
    map_draw(&screen, &view, view.land, view.sea, scratch);
    memcpy(whole, bits, sizeof bits);
    /* the same map through a viewport that cuts it on every side */
    fresh(0xFF);
    gfx_t part;
    gfx_viewport(&screen, &part, 100, 60, 200, 150);
    map_view_t moved = view;
    moved.x -= 100;
    moved.y -= 60;
    part.shade = 9;
    map_draw(&part, &moved, view.land, view.sea, scratch);
    CHECK(part.shade == 9); /* the colour stays */
    int differ = 0, outside = 0;
    for (int y = 0; y < SCREEN_H; y++) {
        for (int x = 0; x < SCREEN_W; x++) {
            bool inside = x >= 100 && x < 300 && y >= 60 && y < 210;
            bool now = gfx_get(&screen, x, y), before = (whole[y * 50 + x / 8] & (0x80 >> (x % 8))) == 0;
            if (!inside) {
                outside += now;
            } else if (x > 100 && x < 299 && y > 60 && y < 209) {
                /* a coast along the cut is not drawn: only its edge may differ */
                differ += now != before;
            }
        }
    }
    CHECK(outside == 0);
    CHECK(differ == 0);
    /* a map wholly off the surface draws nothing */
    fresh(0xFF);
    view.x = 4000;
    map_draw(&screen, &view, 16, 0, scratch);
    bool blank = true;
    for (size_t at = 0; at < sizeof bits; at++) {
        blank = blank && bits[at] == 0xFF;
    }
    CHECK(blank);
}

static void test_shades(void)
{
    map_view_t view = WORLD;
    view.flags = MAP_COASTLINE;
    fresh(0x00);
    map_draw(&screen, &view, 4, 0, scratch);
    int ink = 0, coast = 0;
    for (int py = 1; py < 259; py++) {
        for (int px = 1; px < 399; px++) {
            ink += gfx_get(&screen, px, py);
        }
    }
    /* Moscow is inland: land shade 4 inks a quarter of its 4 x 4 pattern */
    int x0 = (int)((37.6 + 180) / 360 * 400) & ~3, y0 = (int)((90 - 55.7) / 180 * 260) & ~3;
    int square = 0;
    for (int y = y0; y < y0 + 4; y++) {
        for (int x = x0; x < x0 + 4; x++) {
            square += gfx_get(&screen, x, y);
        }
    }
    CHECK(square == 4);
    /* the Atlantic is paper */
    int px, py;
    reference_point(&view, 30, -40, &px, &py);
    CHECK(!gfx_get(&screen, px, py));
    /* the coast is solid: Africa's west coast at the equator's latitude band */
    for (int x = 0; x < 400; x++) {
        coast += gfx_get(&screen, x, 130);
    }
    CHECK(coast > 0 && ink > 0);
}

static int ink_in(int x0, int y0, int x1, int y1)
{
    int n = 0;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            if (x >= 0 && y >= 0 && x < SCREEN_W && y < SCREEN_H) {
                n += gfx_get(&screen, x, y);
            }
        }
    }
    return n;
}

static void test_rings(void)
{
    /* a ring round the centre is a circle of radius / scale pixels */
    map_view_t view = aeqd(34.7071, 33.0226, 1316);
    view.x = 0;
    view.y = 20;
    fresh(0xFF);
    map_draw_ring(&screen, &view, view.lat_e6, view.lon_e6, 150000);
    int r = (int)lround(150000.0 / 1316); /* 114 */
    CHECK(ink_in(200 + r - 1, 150 - 1, 200 + r + 1, 150 + 1) > 0);
    CHECK(ink_in(200 - r - 1, 150 - 1, 200 - r + 1, 150 + 1) > 0);
    CHECK(ink_in(200 - 1, 150 - r - 1, 200 + 1, 150 - r + 1) > 0);
    CHECK(ink_in(190, 140, 210, 160) == 0);
    CHECK(ink_in(0, 0, 399, 19) == 0 && ink_in(0, 280, 399, 299) == 0); /* outside the rectangle */
    /* larger than the rectangle: clipped to it */
    fresh(0xFF);
    view = aeqd(34.7071, 33.0226, 1316);
    view.y = 20;
    map_draw_ring(&screen, &view, view.lat_e6, view.lon_e6, 200000);
    CHECK(ink_in(0, 0, 399, 19) == 0 && ink_in(0, 280, 399, 299) == 0);
    CHECK(ink_in(0, 20, 399, 279) > 0);
    /* across the world map's edge: both sides, no line across the map */
    fresh(0xFF);
    map_draw_ring(&screen, &WORLD, -17000000, 179500000, 1000000);
    CHECK(ink_in(0, 120, 20, 150) > 0);
    CHECK(ink_in(379, 120, 399, 150) > 0);
    CHECK(ink_in(40, 0, 360, 259) == 0);
    /* round a pole: a line across the whole width */
    fresh(0xFF);
    map_draw_ring(&screen, &WORLD, 90000000, 0, 1000000);
    int across = 0;
    for (int x = 0; x < 400; x++) {
        across += ink_in(x, 0, x, 40) > 0;
    }
    CHECK(across == 400);
    /* Far-side and antipodal rings can fail individual projections. */
    fresh(0xFF);
    map_draw_ring(&screen, &view, -view.lat_e6, view.lon_e6 > 0 ? view.lon_e6 - 180000000 : view.lon_e6 + 180000000, 2000000);
    CHECK(ink_in(0, 0, 399, 299) == 0);
    /* the colour is the surface's */
    fresh(0xFF);
    screen.shade = 0;
    map_draw_ring(&screen, &view, view.lat_e6, view.lon_e6, 150000);
    CHECK(ink_in(0, 0, 399, 299) == 0);
}

int main(void)
{
    test_every_row();
    test_places();
    test_projection();
    test_points();
    test_validation();
    test_render();
    test_render_clipped();
    test_shades();
    test_rings();
    for (int row = 0; row < MAP_LAND_HEIGHT; row++) {
        free(all_rows[row]);
    }
    REPORT();
}
