/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * How long the map service takes to draw the views applications use, on this computer, and
 * what that comes to on the ESP32-C6 by what the drawing does: rANS symbols decoded, pixels
 * answered, land cells interpolated, places projected exactly. `make bench_map`, then
 * `./bench_map [folder]`; with a folder it also writes each view as a PBM picture.
 *
 * The C6's cycles per operation are estimates, from the instructions -Os makes of each inner
 * loop for RV32IMAC (riscv32-esp-elf-objdump) at about one cycle each, code and tables read
 * from flash through the cache. Not measured on the chip: take them as half to twice that.
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "map.h"

long map_tally_symbols, map_tally_pixels, map_tally_cells, map_tally_exact;

#define SYMBOL_CYCLES 45  /* a symbol and what its code does to the row */
#define PIXEL_CYCLES  120 /* the row's binary search, the pixel kept, read back and shaded */
#define CELL_CYCLES   60  /* a bilinear land cell */
#define EXACT_CYCLES  800 /* two CORDICs of 24 steps, two series, the vector */
#define C6_HZ         160000000.0

static uint8_t bits[400 / 8 * 300];
static uint8_t scratch[MAP_SCRATCH_BYTES];

typedef struct {
    const char *name;
    map_view_t  view;
    int32_t     ring_m;
} sample_t;

static const sample_t SAMPLES[] = {
    { "world", { .w = 400, .h = 260, .projection = MAP_WORLD, .land = 4, .flags = MAP_COASTLINE }, 0 },
    { "tokyo-2000km", { .w = 400, .h = 260, .lat_e6 = 35676200, .lon_e6 = 139650300, .m_per_px = 17544,
                        .projection = MAP_AEQD, .land = 4, .flags = MAP_COASTLINE }, 2000000 },
    { "istanbul-300km", { .w = 400, .h = 260, .lat_e6 = 41008200, .lon_e6 = 28978400, .m_per_px = 2632,
                          .projection = MAP_AEQD, .land = 4, .flags = MAP_COASTLINE }, 300000 },
    { "moscow-1000km", { .w = 400, .h = 260, .lat_e6 = 55755800, .lon_e6 = 37617300, .m_per_px = 8772,
                         .projection = MAP_AEQD, .land = 4, .flags = MAP_COASTLINE }, 1000000 },
    { "limassol-150km", { .w = 400, .h = 260, .lat_e6 = 34707100, .lon_e6 = 33022600, .m_per_px = 1316,
                          .projection = MAP_AEQD, .land = 4, .flags = MAP_COASTLINE }, 150000 },
    { "north-pole-25km", { .w = 400, .h = 260, .lat_e6 = 85000000, .lon_e6 = 0, .m_per_px = 25000,
                           .projection = MAP_AEQD, .land = 4, .flags = MAP_COASTLINE }, 0 },
};

static void write_pbm(const char *folder, const char *name, const gfx_t *g, int h)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.pbm", folder, name);
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        perror(path);
        exit(1);
    }
    fprintf(file, "P4\n400 %d\n", h);
    for (int at = 0; at < 50 * h; at++) {
        fputc(~g->bits[at] & 0xFF, file); /* PBM: 1 is black */
    }
    fclose(file);
}

int main(int argc, char **argv)
{
    const char *folder = argc > 1 ? argv[1] : NULL;
    printf("%-16s %9s %9s %8s %8s %8s %9s\n", "view", "host ms", "symbols", "pixels", "cells", "exact", "C6 ms");
    for (size_t index = 0; index < sizeof SAMPLES / sizeof SAMPLES[0]; index++) {
        const sample_t *sample = &SAMPLES[index];
        gfx_t g;
        gfx_init(&g, bits, 400, 300);
        gfx_clear(&g, false);
        map_tally_symbols = map_tally_pixels = map_tally_cells = map_tally_exact = 0;
        map_draw(&g, &sample->view, sample->view.land, sample->view.sea, scratch);
        long symbols = map_tally_symbols, pixels = map_tally_pixels, cells = map_tally_cells, exact = map_tally_exact;
        enum { TURNS = 20 };
        clock_t start = clock();
        for (int turn = 0; turn < TURNS; turn++) {
            map_draw(&g, &sample->view, sample->view.land, sample->view.sea, scratch);
        }
        double host_ms = 1000.0 * (double)(clock() - start) / CLOCKS_PER_SEC / TURNS;
        double cycles = (double)symbols * SYMBOL_CYCLES + (double)pixels * PIXEL_CYCLES +
                        (double)cells * CELL_CYCLES + (double)exact * EXACT_CYCLES;
        printf("%-16s %9.2f %9ld %8ld %8ld %8ld %9.0f\n", sample->name, host_ms, symbols, pixels, cells, exact,
               cycles / C6_HZ * 1000);
        if (sample->ring_m > 0) {
            map_draw_ring(&g, &sample->view, sample->view.lat_e6, sample->view.lon_e6, sample->ring_m);
        }
        if (folder != NULL) {
            write_pbm(folder, sample->name, &g, 260);
        }
    }
    return 0;
}
