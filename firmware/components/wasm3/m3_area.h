/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The application area: the one piece of memory an application lives in.
 * Its page of 64 KB and everything the interpreter needs to run it - the
 * compiled code, the stack, its own records - are taken from here and from
 * nowhere else, so that the host's heap is the same whichever application
 * is on the screen, and an application that needs more than there is
 * fails to start, every time, instead of now and then. */
#define M3_AREA_PAGE_BYTES    (64 * 1024)
/* The owner enlarged interpreter space from 64 KB to 80 KB on 2026-11-24.
 * This includes allocator overhead, compiled code, interpreter records and
 * contract scratch memory. Code-size estimates guide builds with warnings;
 * actual allocation peaks depend on the paths an application runs. */
#define M3_AREA_WORKING_BYTES (80 * 1024)
#define M3_AREA_BYTES         (M3_AREA_PAGE_BYTES + M3_AREA_WORKING_BYTES)

/* Takes the area from the heap, once, for as long as the device runs.
 * Call it first of all, while the heap is whole. */
void m3_area_start(void);

/* Makes the area whole again: every instance starts in an area nobody has
 * been in. Only with no application loaded. */
void m3_area_reset(void);

/* The most of the area that was in use since the last reset, for sizing
 * it; 0 without an area. */
size_t m3_area_most_used(void);

/* Memory of the area lent to one call of the contract for as long as the
 * call lasts - the map's working memory - so that it is counted against the
 * application, as the interpreter's own memory is, and not taken from the
 * host's heap. NULL when the area has no room. */
void *m3_area_take(size_t size);
void  m3_area_give(void *memory);

#ifdef __cplusplus
}
#endif
