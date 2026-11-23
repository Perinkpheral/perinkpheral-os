/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include <stdlib.h>

#include "m3_area.h"
#include "multi_heap.h"
#include "tinytest.h"

static bool refuse_allocation;
static bool refuse_registration;
static int reservations;
static int registrations;
static int releases;
static int heap_allocations;
static size_t minimum_free_bytes;

static void *reserve_area(size_t size)
{
    CHECK(size == M3_AREA_BYTES);
    reservations++;
    return refuse_allocation ? NULL : malloc(size);
}

static void release_area(void *memory)
{
    if (memory != NULL) {
        releases++;
    }
    free(memory);
}

#define malloc reserve_area
#define free release_area
#include "../../components/wasm3/m3_area.c"
#undef malloc
#undef free

multi_heap_handle_t multi_heap_register(void *memory, size_t size)
{
    CHECK(memory != NULL && size == M3_AREA_BYTES);
    registrations++;
    minimum_free_bytes = size;
    return refuse_registration ? NULL : memory;
}

void multi_heap_get_info(multi_heap_handle_t heap, multi_heap_info_t *info)
{
    CHECK(heap != NULL);
    info->minimum_free_bytes = minimum_free_bytes;
}

void *multi_heap_malloc(multi_heap_handle_t heap, size_t size)
{
    CHECK(heap != NULL);
    heap_allocations++;
    return malloc(size);
}

void multi_heap_free(multi_heap_handle_t heap, void *memory)
{
    CHECK(heap != NULL);
    free(memory);
}

void *multi_heap_realloc(multi_heap_handle_t heap, void *memory, size_t size)
{
    CHECK(heap != NULL);
    heap_allocations++;
    return realloc(memory, size);
}

static void check_unavailable(void)
{
    CHECK(!m3_area_available());
    CHECK(m3_area_most_used() == 0);
    CHECK(m3_Area_Alloc(8) == NULL);
    CHECK(m3_Area_Realloc(NULL, 8) == NULL);
    CHECK(m3_area_take(8) == NULL);
    m3_area_give(NULL);
    m3_area_reset();
    CHECK(!m3_area_available());
    CHECK(heap_allocations == 0);
}

int main(void)
{
    check_unavailable();
    refuse_allocation = true;
    CHECK(!m3_area_start());
    CHECK(reservations == 1 && registrations == 0);
    check_unavailable();

    refuse_allocation = false;
    refuse_registration = true;
    CHECK(!m3_area_start());
    CHECK(releases == 1);
    check_unavailable();

    refuse_registration = false;
    CHECK(m3_area_start());
    CHECK(m3_area_available());
    CHECK(reservations == 3 && registrations == 2);
    CHECK(m3_area_start());
    CHECK(reservations == 3 && registrations == 2);
    minimum_free_bytes = M3_AREA_BYTES - 512;
    CHECK(m3_area_most_used() == 512);
    unsigned char *memory = m3_Area_Alloc(8);
    CHECK(memory != NULL);
    for (int index = 0; index < 8; index++) {
        CHECK(memory[index] == 0);
    }
    m3_area_give(memory);
    m3_area_reset();
    CHECK(m3_area_available() && m3_area_most_used() == 0);
    /* The firmware keeps this block for its lifetime. */
    free(s_block);
    REPORT();
}
