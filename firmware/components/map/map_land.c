/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The land's decoder: static rANS over one model (map_land_freq), then each row rebuilt from
 * the row above by the codes tools/perinkpheral map wrote (MapData.kt says what they are).
 */
#include "map_land.h"

#include "map_geo.h"

#include <stddef.h>
#include <string.h>

#define SCALE    (1u << MAP_LAND_SCALE_BITS)
#define RANS_LOW (1u << 23)

void map_land_start(map_land_t *land, int block)
{
    memset(land, 0, offsetof(map_land_t, rows));
    land->block = block;
    land->row = block * MAP_LAND_BLOCK_ROWS - 1;
    land->last = block * MAP_LAND_BLOCK_ROWS + MAP_LAND_BLOCK_ROWS - 1;
    if (land->last >= MAP_LAND_HEIGHT) {
        land->last = MAP_LAND_HEIGHT - 1;
    }
    if (block < 0 || block >= MAP_LAND_BLOCKS) {
        land->is_broken = true;
        return;
    }
    land->at = map_land_stream + map_land_block[block];
    land->end = map_land_stream + map_land_block[block + 1];
    if (land->end - land->at < 4) {
        land->is_broken = true;
        return;
    }
    land->state = (uint32_t)land->at[0] | (uint32_t)land->at[1] << 8 | (uint32_t)land->at[2] << 16 |
                  (uint32_t)land->at[3] << 24;
    land->at += 4;
}

static unsigned symbol(map_land_t *land)
{
    MAP_TALLY(symbols);
    unsigned slot = land->state & (SCALE - 1);
    unsigned s = map_land_symbol[slot];
    land->state = map_land_freq[s] * (land->state >> MAP_LAND_SCALE_BITS) + slot - map_land_cumulative[s];
    while (land->state < RANS_LOW) {
        if (land->at >= land->end) {
            land->is_broken = true;
            return 0;
        }
        land->state = land->state << 8 | *land->at++;
    }
    return s;
}

static unsigned varint(map_land_t *land)
{
    unsigned value = 0;
    for (unsigned shift = 0; shift < 21 && !land->is_broken; shift += 7) {
        unsigned b = symbol(land);
        value |= (b & 127) << shift;
        if (b < 128) {
            return value;
        }
    }
    land->is_broken = true;
    return 0;
}

bool map_land_next(map_land_t *land)
{
    if (land->row >= land->last) {
        return false;
    }
    land->row++;
    int above = land->current;
    int here = 1 - above;
    const uint16_t *previous = land->rows[above];
    int previous_count = land->row % MAP_LAND_BLOCK_ROWS == 0 ? 0 : land->count[above];
    uint16_t *row = land->rows[here];
    int count = 0, next = 0;
    while (!land->is_broken) {
        unsigned code = symbol(land);
        if (code == 0) {
            break;
        }
        int32_t value;
        if (code == 1) {
            next++;
            continue;
        }
        if (code == 2) {
            value = (count > 0 ? row[count - 1] : 0) + (int32_t)varint(land);
        } else {
            unsigned zigzag = code == 3 ? varint(land) : code - 4;
            int32_t delta = (zigzag & 1) ? -(int32_t)((zigzag + 1) >> 1) : (int32_t)(zigzag >> 1);
            value = next < previous_count ? previous[next++] + delta : -1;
        }
        if (count >= MAP_LAND_ROW_MAX || value < 0 || value >= MAP_LAND_WIDTH ||
            (count > 0 && value <= row[count - 1])) {
            land->is_broken = true;
            break;
        }
        row[count++] = (uint16_t)value;
    }
    land->count[here] = land->is_broken ? 0 : count;
    land->current = here;
    return true;
}

const uint16_t *map_land_row(const map_land_t *land, int *count)
{
    *count = land->count[land->current];
    return land->rows[land->current];
}

bool map_land_at(const uint16_t *row, int count, int column)
{
    /* land where an odd number of transitions are at or before the column */
    int low = 0, high = count;
    while (low < high) {
        int middle = (low + high) / 2;
        if (row[middle] <= column) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return (low & 1) != 0;
}
