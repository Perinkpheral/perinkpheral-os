/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "map_land_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The land, read a row at a time (map_land_data.c, made by tools/perinkpheral map). A row is
 * the columns where land and sea change, from sea at column 0. Rows come in blocks of
 * MAP_LAND_BLOCK_ROWS, each coded against the one above it, so a row is read by starting its
 * block and reading on to it; the reader keeps two rows and nothing else. */
typedef struct {
    const uint8_t *at, *end;
    uint32_t       state;
    int            block;
    int            row;   /* the absolute row last read; -1 before the first */
    int            last;  /* the block's last row */
    int            count[2];
    int            current;
    bool           is_broken; /* the bytes said something impossible: rows read as sea */
    uint16_t       rows[2][MAP_LAND_ROW_MAX];
} map_land_t;

/* Ready to read the first row of a block, 0 .. MAP_LAND_BLOCKS - 1. */
void map_land_start(map_land_t *land, int block);

/* Reads the block's next row; false past its last. */
bool map_land_next(map_land_t *land);

/* The row last read: its transitions and how many there are. */
const uint16_t *map_land_row(const map_land_t *land, int *count);

/* True where a row's cell `column` is land. */
bool map_land_at(const uint16_t *row, int count, int column);

#ifdef __cplusplus
}
#endif
