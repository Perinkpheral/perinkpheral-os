/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Included before the map's sources by bench_map: what they do, counted.
 */
#pragma once

extern long map_tally_symbols, map_tally_pixels, map_tally_cells, map_tally_exact;
#define MAP_TALLY(what) ((void)map_tally_##what++)
