/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Drawing the land. A row of the land is up to 63 rows of decoding away, and a row of pixels
 * of an azimuthal map crosses hundreds of rows of land, so pixels are visited in the land's
 * order, not the screen's: a band of 32 screen rows at a time, a cursor walks each column of
 * it in the direction its land rows grow, and each block of land is decoded once a band while
 * the cursors waiting on its rows are answered. A column whose latitude turns - one near a
 * pole - gets two cursors. What a pixel is (land or sea) is kept in the surface itself until
 * the map is done; the coastline and the shades are then put on row by row.
 *
 * Projection: an azimuthal pixel's land cell is computed exactly every 8 pixels and in between
 * interpolated, except in a square of the grid whose centre interpolation would put more than
 * a tenth of a pixel off - near a pole, where longitude turns fast - where every pixel is
 * computed. Interpolated, a column's land rows are straight between the grid's rows, so where
 * they turn is found at those rows alone.
 */
#include "map.h"

#include <string.h>

#include "map_land.h"

#define BAND        32
#define GRID        8
#define GRID_ROWS   (BAND / GRID + 1)
#define GRID_COLUMNS ((MAP_WIDTH_MAX - 1) / GRID + 2)
#define NONE        0xFFFF
#define FRACTION    MAP_CELL_FRACTION_BITS
#define CELLS_ACROSS ((int32_t)MAP_LAND_WIDTH << FRACTION)
/* Longitude turning more than this across a square of the grid - 1/8 radian, 21600 / 16 pi
 * cells - is too near a pole to be unwrapped and interpolated at all. */
#define EXACT_SPREAD ((int32_t)430 << FRACTION)
/* Metres a cell is tall: 2 pi R / 21600. */
#define CELL_M 1853
/* How far interpolation may move a pixel's centre: a tenth of a pixel. */
#define TOLERANCE 10

_Static_assert(MAP_LAND_WIDTH == 21600 && MAP_LAND_HEIGHT == 10800, "map_geo.c counts one arc-minute cells");

typedef struct {
    uint8_t  y, last; /* rows of the band: where it is, where it stops */
    uint16_t next;
    uint16_t column;  /* the land column of the pixel it waits with */
} cursor_t;

typedef struct {
    map_land_t land;
    uint16_t   block_head[MAP_LAND_BLOCKS];
    uint16_t   row_head[MAP_LAND_BLOCK_ROWS];
    int32_t    grid[GRID_ROWS][GRID_COLUMNS][2]; /* row, column of the grid's pixels */
    uint8_t    exact[GRID_ROWS - 1][GRID_COLUMNS - 1];
    cursor_t   cursors[2 * MAP_WIDTH_MAX];
} scratch_t;

_Static_assert(sizeof(scratch_t) <= MAP_SCRATCH_BYTES, "MAP_SCRATCH_BYTES holds the renderer's scratch");

typedef struct {
    gfx_t             *g;
    const map_view_t  *view;
    map_frame_t        frame;
    scratch_t         *s;
    int                x0, x1;     /* the visible columns of the rectangle */
    int                band_top;   /* rows from the rectangle's top */
    int                grid_x0, grid_y0;
} drawing_t;

static int32_t unwrap(int32_t column, int32_t near)
{
    if (column - near > CELLS_ACROSS / 2) {
        return column - CELLS_ACROSS;
    }
    if (near - column > CELLS_ACROSS / 2) {
        return column + CELLS_ACROSS;
    }
    return column;
}

static void prepare_grid(drawing_t *d, int y1)
{
    scratch_t *s = d->s;
    d->grid_x0 = d->x0 / GRID;
    d->grid_y0 = d->band_top / GRID;
    int columns = (d->x1 - 1) / GRID + 2 - d->grid_x0;
    int rows = (y1 - 1) / GRID + 2 - d->grid_y0;
    for (int gy = 0; gy < rows; gy++) {
        for (int gx = 0; gx < columns; gx++) {
            map_pixel_cell(d->view, &d->frame, (d->grid_x0 + gx) * GRID, (d->grid_y0 + gy) * GRID,
                           &s->grid[gy][gx][0], &s->grid[gy][gx][1]);
        }
    }
    for (int gy = 0; gy + 1 < rows; gy++) {
        for (int gx = 0; gx + 1 < columns; gx++) {
            int32_t first = s->grid[gy][gx][1], low = first, high = first;
            int32_t corners[3] = { s->grid[gy][gx + 1][1], s->grid[gy + 1][gx][1], s->grid[gy + 1][gx + 1][1] };
            for (int corner = 0; corner < 3; corner++) {
                int32_t column = unwrap(corners[corner], first);
                low = column < low ? column : low;
                high = column > high ? column : high;
            }
            if (high - low > EXACT_SPREAD) {
                s->exact[gy][gx] = true;
                continue;
            }
            /* the square's centre, exactly and as interpolated */
            int32_t row, column;
            map_pixel_cell(d->view, &d->frame, (d->grid_x0 + gx) * GRID + GRID / 2,
                           (d->grid_y0 + gy) * GRID + GRID / 2, &row, &column);
            int64_t mean_row = ((int64_t)s->grid[gy][gx][0] + s->grid[gy][gx + 1][0] + s->grid[gy + 1][gx][0] +
                                s->grid[gy + 1][gx + 1][0]) / 4;
            int64_t mean_column = ((int64_t)first + unwrap(corners[0], first) + unwrap(corners[1], first) +
                                   unwrap(corners[2], first)) / 4;
            int64_t off_row = row - mean_row, off_column = unwrap(column, first) - mean_column;
            off_row = off_row < 0 ? -off_row : off_row;
            off_column = off_column < 0 ? -off_column : off_column;
            /* a column is narrower by the cosine of its latitude */
            off_column = (off_column * map_row_cosine(row)) >> 30;
            int64_t allowed = ((int64_t)d->view->m_per_px << FRACTION) / (TOLERANCE * CELL_M);
            s->exact[gy][gx] = off_row > allowed || off_column > allowed;
        }
    }
}

/* The land cell of a pixel of the rectangle: its row and column. */
static void cell_at(const drawing_t *d, int px, int py, int *row, int *column)
{
    int32_t r, c;
    const scratch_t *s = d->s;
    MAP_TALLY(cells);
    int gx = px / GRID - d->grid_x0, gy = py / GRID - d->grid_y0;
    if (d->view->projection == MAP_WORLD || s->exact[gy][gx]) {
        map_pixel_cell(d->view, &d->frame, px, py, &r, &c);
    } else {
        int dx = px % GRID, dy = py % GRID;
        int w00 = (GRID - dx) * (GRID - dy), w10 = dx * (GRID - dy), w01 = (GRID - dx) * dy, w11 = dx * dy;
        const int32_t *p00 = s->grid[gy][gx], *p10 = s->grid[gy][gx + 1];
        const int32_t *p01 = s->grid[gy + 1][gx], *p11 = s->grid[gy + 1][gx + 1];
        r = (p00[0] * w00 + p10[0] * w10 + p01[0] * w01 + p11[0] * w11) >> 6;
        c = (p00[1] * w00 + unwrap(p10[1], p00[1]) * w10 + unwrap(p01[1], p00[1]) * w01 +
             unwrap(p11[1], p00[1]) * w11) >> 6;
        c = c < 0 ? c + CELLS_ACROSS : c >= CELLS_ACROSS ? c - CELLS_ACROSS : c;
    }
    r >>= FRACTION;
    *row = r < 0 ? 0 : r >= MAP_LAND_HEIGHT ? MAP_LAND_HEIGHT - 1 : r;
    *column = c >> FRACTION;
}

static int row_at(const drawing_t *d, int px, int py)
{
    int row, column;
    cell_at(d, px, py, &row, &column);
    return row;
}

static void push(uint16_t *head, cursor_t *cursors, int index)
{
    cursors[index].next = *head;
    *head = (uint16_t)index;
}

/* Puts a cursor with the block its pixel's land row is in. */
static void wait_in_block(drawing_t *d, int index, int px)
{
    int row, column;
    cursor_t *cursor = &d->s->cursors[index];
    cell_at(d, px, d->band_top + cursor->y, &row, &column);
    cursor->column = (uint16_t)column;
    push(&d->s->block_head[row / MAP_LAND_BLOCK_ROWS], d->s->cursors, index);
}

/* The next row of a column to look at for where its land rows turn: every row where pixels are
 * computed, else - straight between them - the grid's next row or the band's last. */
static int next_look(int y, int y1, bool every)
{
    int knot = (y / GRID + 1) * GRID;
    return every ? y + 1 : knot < y1 - 1 ? knot : y < y1 - 1 ? y1 - 1 : y1;
}

/* The cursors of a column of the band: from its lowest land row outwards when its latitude
 * peaks inside the band, towards its highest when it dips, else one along it. */
static void start_column(drawing_t *d, int index, int y0, int y1)
{
    int px = d->x0 + index;
    int columns = d->x1 - d->x0;
    int lowest = y0, highest = y0, low = row_at(d, px, y0), high = low;
    if (d->view->projection != MAP_WORLD) {
        bool is_exact = false;
        for (int gy = y0 / GRID; gy <= (y1 - 1) / GRID; gy++) {
            is_exact = is_exact || d->s->exact[gy - d->grid_y0][px / GRID - d->grid_x0];
        }
        for (int y = next_look(y0, y1, is_exact); y < y1; y = next_look(y, y1, is_exact)) {
            int row = row_at(d, px, y);
            if (row < low) {
                low = row;
                lowest = y;
            }
            if (row > high) {
                high = row;
                highest = y;
            }
        }
    }
    int starts[2], ends[2], count = 0;
    if (lowest > y0 && lowest < y1 - 1) {
        starts[count] = lowest;
        ends[count++] = y1 - 1;
        starts[count] = lowest - 1;
        ends[count++] = y0;
    } else if (highest > y0 && highest < y1 - 1) {
        starts[count] = y0;
        ends[count++] = highest;
        starts[count] = y1 - 1;
        ends[count++] = highest + 1;
    } else if (lowest == y0) {
        starts[count] = y0;
        ends[count++] = y1 - 1;
    } else {
        starts[count] = y1 - 1;
        ends[count++] = y0;
    }
    for (int k = 0; k < count; k++) {
        int cursor = index + k * columns;
        d->s->cursors[cursor].y = (uint8_t)(starts[k] - d->band_top);
        d->s->cursors[cursor].last = (uint8_t)(ends[k] - d->band_top);
        wait_in_block(d, cursor, px);
    }
}

/* What a pixel is, held in the surface until the band is done: ink for land. */
static void keep(drawing_t *d, int px, int py, bool is_land)
{
    gfx_pixel(d->g, d->view->x + px, d->view->y + py, is_land);
}

/* Answers a cursor's pixel from the row just read, and the pixels after it while their land
 * is on that row; then puts the cursor where its next pixel waits: a later row of this block,
 * or a later block. */
static void walk(drawing_t *d, int index, int block, int read, int *pending)
{
    scratch_t *s = d->s;
    int columns = d->x1 - d->x0;
    int px = d->x0 + (index < columns ? index : index - columns);
    cursor_t *cursor = &s->cursors[index];
    int count;
    const uint16_t *row = map_land_row(&s->land, &count);
    int column = cursor->column;
    for (;;) {
        MAP_TALLY(pixels);
        keep(d, px, d->band_top + cursor->y, map_land_at(row, count, column));
        if (cursor->y == cursor->last) {
            return;
        }
        cursor->y += cursor->last > cursor->y ? 1 : -1;
        int land_row;
        cell_at(d, px, d->band_top + cursor->y, &land_row, &column);
        /* a row behind the one read is a rounding wobble a row or so back: the row read answers */
        if (land_row > read) {
            /* rows only grow along a cursor, so this one waits */
            cursor->column = (uint16_t)column;
            if (land_row / MAP_LAND_BLOCK_ROWS == block) {
                int at = land_row % MAP_LAND_BLOCK_ROWS;
                push(&s->row_head[at], s->cursors, index);
                *pending = at > *pending ? at : *pending;
            } else {
                push(&s->block_head[land_row / MAP_LAND_BLOCK_ROWS], s->cursors, index);
            }
            return;
        }
    }
}

static void draw_band(drawing_t *d, int y0, int y1)
{
    scratch_t *s = d->s;
    int columns = d->x1 - d->x0;
    if (d->view->projection != MAP_WORLD) {
        prepare_grid(d, y1);
    }
    memset(s->block_head, 0xFF, sizeof s->block_head);
    for (int index = 0; index < columns; index++) {
        start_column(d, index, y0, y1);
    }
    for (int block = 0; block < MAP_LAND_BLOCKS; block++) {
        if (s->block_head[block] == NONE) {
            continue;
        }
        memset(s->row_head, 0xFF, sizeof s->row_head);
        int pending = -1;
        for (uint16_t index = s->block_head[block]; index != NONE;) {
            uint16_t next = s->cursors[index].next;
            int row, column;
            cell_at(d, d->x0 + (index < columns ? index : index - columns), d->band_top + s->cursors[index].y,
                    &row, &column);
            int at = row % MAP_LAND_BLOCK_ROWS;
            push(&s->row_head[at], s->cursors, index);
            pending = at > pending ? at : pending;
            index = next;
        }
        s->block_head[block] = NONE;
        map_land_start(&s->land, block);
        for (int at = 0; at <= pending && map_land_next(&s->land); at++) {
            while (s->row_head[at] != NONE) {
                uint16_t index = s->row_head[at];
                s->row_head[at] = s->cursors[index].next;
                walk(d, index, block, block * MAP_LAND_BLOCK_ROWS + at, &pending);
            }
        }
    }
}

static bool bit(const uint8_t *row, int x)
{
    return (row[x >> 3] >> (x & 7)) & 1;
}

/* The land of a row of the rectangle as the bands left it, a bit a pixel. */
static void read_row(drawing_t *d, int py, uint8_t *row)
{
    memset(row, 0, (size_t)(d->x1 - d->x0 + 7) / 8);
    for (int px = d->x0; px < d->x1; px++) {
        if (gfx_get(d->g, d->view->x + px, d->view->y + py)) {
            row[(px - d->x0) >> 3] |= (uint8_t)(1 << ((px - d->x0) & 7));
        }
    }
}

/* Shades and the coastline. Past the visible part a neighbour is taken to be the pixel itself,
 * so a coast is not drawn along the map's edges. */
static void finish(drawing_t *d, int y0, int y1, int land, int sea)
{
    int width = d->x1 - d->x0, bytes = (width + 7) / 8;
    uint8_t *rows = (uint8_t *)d->s->cursors;
    uint8_t *above = rows, *here = rows + bytes, *below = rows + 2 * bytes;
    bool coast = (d->view->flags & MAP_COASTLINE) != 0;
    read_row(d, y0, here);
    memcpy(above, here, (size_t)bytes);
    for (int py = y0; py < y1; py++) {
        if (py + 1 < y1) {
            read_row(d, py + 1, below);
        } else {
            memcpy(below, here, (size_t)bytes);
        }
        for (int x = 0; x < width; x++) {
            bool is_land = bit(here, x);
            int shade = sea;
            if (is_land) {
                bool is_coast = coast && (!bit(above, x) || !bit(below, x) ||
                                          (x > 0 && !bit(here, x - 1)) ||
                                          (x + 1 < width && !bit(here, x + 1)));
                shade = is_coast ? GFX_SHADE_SOLID : land;
            }
            d->g->shade = (uint8_t)shade;
            gfx_pixel(d->g, d->view->x + d->x0 + x, d->view->y + py, shade > 0);
        }
        uint8_t *spare = above;
        above = here;
        here = below;
        below = spare;
    }
}

void map_draw(gfx_t *g, const map_view_t *view, int land, int sea, void *scratch)
{
    drawing_t d = { .g = g, .view = view, .s = scratch };
    int left = g->clip_x0 - g->origin_x - view->x, right = g->clip_x1 - g->origin_x - view->x;
    int top = g->clip_y0 - g->origin_y - view->y, bottom = g->clip_y1 - g->origin_y - view->y;
    d.x0 = left > 0 ? left : 0;
    d.x1 = right < view->w ? right : view->w;
    int y0 = top > 0 ? top : 0, y1 = bottom < view->h ? bottom : view->h;
    if (d.x0 >= d.x1 || y0 >= y1) {
        return;
    }
    uint8_t colour = g->shade;
    g->shade = GFX_SHADE_SOLID;
    if (view->projection != MAP_WORLD) {
        map_frame(view, &d.frame);
    }
    for (d.band_top = y0 / BAND * BAND; d.band_top < y1; d.band_top += BAND) {
        int from = d.band_top > y0 ? d.band_top : y0;
        int to = d.band_top + BAND < y1 ? d.band_top + BAND : y1;
        draw_band(&d, from, to);
    }
    finish(&d, y0, y1, land, sea);
    g->shade = colour;
}

/* ---- rings ---- */

/* A piece of the ring in the rectangle's pixels, clipped to it (Cohen-Sutherland). */
static void piece(gfx_t *g, const map_view_t *view, int64_t x0, int64_t y0, int64_t x1, int64_t y1)
{
    int64_t right = view->w - 1, bottom = view->h - 1;
    for (int turn = 0; turn < 8; turn++) {
        int out0 = (x0 < 0) | (x0 > right) << 1 | (y0 < 0) << 2 | (y0 > bottom) << 3;
        int out1 = (x1 < 0) | (x1 > right) << 1 | (y1 < 0) << 2 | (y1 > bottom) << 3;
        if ((out0 | out1) == 0) {
            gfx_draw_line(g, view->x + (int)x0, view->y + (int)y0, view->x + (int)x1, view->y + (int)y1, true);
            return;
        }
        if (out0 & out1) {
            return;
        }
        int out = out0 ? out0 : out1;
        int64_t x, y;
        if (out & 8) {
            x = x0 + (x1 - x0) * (bottom - y0) / (y1 - y0);
            y = bottom;
        } else if (out & 4) {
            x = x0 + (x1 - x0) * (0 - y0) / (y1 - y0);
            y = 0;
        } else if (out & 2) {
            y = y0 + (y1 - y0) * (right - x0) / (x1 - x0);
            x = right;
        } else {
            y = y0 + (y1 - y0) * (0 - x0) / (x1 - x0);
            x = 0;
        }
        if (out == out0) {
            x0 = x;
            y0 = y;
        } else {
            x1 = x;
            y1 = y;
        }
    }
}

static void piece256(gfx_t *g, const map_view_t *view, int64_t x0, int64_t y0, int64_t x1, int64_t y1)
{
    piece(g, view, x0 >> 8, y0 >> 8, x1 >> 8, y1 >> 8);
}

#define RING_PIECES 256

void map_draw_ring(gfx_t *g, const map_view_t *view, int32_t lat_e6, int32_t lon_e6, int32_t radius_m)
{
    map_frame_t frame = { 0 };
    if (view->projection != MAP_WORLD) {
        map_frame(view, &frame);
    }
    int32_t ax, ay, bx, by;
    bool had = map_ring_point(view, &frame, lat_e6, lon_e6, radius_m, 0, RING_PIECES, &ax, &ay);
    int64_t width256 = (int64_t)view->w * 256;
    for (int index = 1; index <= RING_PIECES; index++) {
        bool has = map_ring_point(view, &frame, lat_e6, lon_e6, radius_m, index % RING_PIECES,
                                  RING_PIECES, &bx, &by);
        if (had && has) {
            int64_t across = (int64_t)bx - ax;
            if (view->projection == MAP_WORLD && (across > width256 / 2 || -across > width256 / 2)) {
                /* through the edge: to it, and on from the other side */
                int64_t edge = across > 0 ? 0 : width256;
                int64_t shifted = across > 0 ? bx - width256 : bx + width256;
                int64_t y = ay + (by - ay) * (edge - ax) / (shifted - ax);
                piece256(g, view, ax, ay, edge, y);
                piece256(g, view, width256 - edge, y, bx, by);
            } else {
                piece256(g, view, ax, ay, bx, by);
            }
        }
        ax = bx;
        ay = by;
        had = has;
    }
}
