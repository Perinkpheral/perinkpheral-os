/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "map_land.h"

#include "tinytest.h"

/* Substitute malformed tables so the sanitizers also check pointer formation. */
const uint32_t map_land_block[MAP_LAND_BLOCKS + 1] = {
    MAP_LAND_STREAM_BYTES, MAP_LAND_STREAM_BYTES + 4,
    5, 4,
    0, 3,
    UINT32_MAX - 4, UINT32_MAX,
    0, 4,
    MAP_LAND_STREAM_BYTES - 4, MAP_LAND_STREAM_BYTES,
    0, 0,
};
const uint8_t map_land_stream[MAP_LAND_STREAM_BYTES] = {
    [0] = 0x78, [1] = 0x56, [2] = 0x34, [3] = 0x12,
    [MAP_LAND_STREAM_BYTES - 4] = 0xEF, [MAP_LAND_STREAM_BYTES - 3] = 0xCD,
    [MAP_LAND_STREAM_BYTES - 2] = 0xAB, [MAP_LAND_STREAM_BYTES - 1] = 0x09,
};
const uint16_t map_land_freq[256] = { 0 };
const uint16_t map_land_cumulative[256] = { 0 };
const uint8_t map_land_symbol[1 << MAP_LAND_SCALE_BITS] = { 0 };

static void test_malformed_blocks(void)
{
    const int broken_blocks[] = { 0, 2, 4, 6, 12 };
    for (unsigned index = 0; index < sizeof(broken_blocks) / sizeof(broken_blocks[0]); index++) {
        map_land_t land;
        map_land_start(&land, broken_blocks[index]);
        CHECK(land.is_broken);
        CHECK(land.at == NULL && land.end == NULL);
        CHECK(map_land_next(&land));
        int count;
        map_land_row(&land, &count);
        CHECK(count == 0);
    }
}

static void test_boundary_blocks(void)
{
    map_land_t land;
    map_land_start(&land, 8);
    CHECK(!land.is_broken);
    CHECK(land.at == map_land_stream + 4);
    CHECK(land.end == land.at);
    CHECK(land.state == 0x12345678);

    map_land_start(&land, 10);
    CHECK(!land.is_broken);
    CHECK(land.at == map_land_stream + MAP_LAND_STREAM_BYTES);
    CHECK(land.end == land.at);
    CHECK(land.state == 0x09ABCDEF);
}

int main(void)
{
    test_malformed_blocks();
    test_boundary_blocks();
    REPORT();
}
