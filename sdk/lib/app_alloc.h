/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Arena allocator for application memory.
 *
 * An application's memory is as large as its stack and static data
 * (tools/perinkpheral build reports the size), and cannot grow. The arena is
 * part of that data: an application that allocates declares its size once,
 * in one source file, with APP_ALLOC_ARENA(bytes). Without the declaration
 * the application does not link.
 * Allocations are sequential and are released together, back to a mark.
 * There is no free() for a single allocation.
 *
 *     APP_ALLOC_ARENA(4096);
 *
 *     int32_t   before = app_alloc_mark();
 *     event_t  *events = app_alloc(count * sizeof *events);
 *     if (events == NULL) { ... }      // not enough memory
 *     ...
 *     app_alloc_release(before);       // releases `events` and everything allocated after it
 */
#pragma once

#include <stdint.h>

/* Declares the arena of `bytes` bytes, rounded up to eight. Once, at file scope. */
#define APP_ALLOC_ARENA(bytes)                                              \
    _Alignas(8) unsigned char app_alloc_arena[((bytes) + 7) & ~7] = {0};      \
    const int32_t             app_alloc_arena_bytes = ((bytes) + 7) & ~7

/* Returns `bytes` of memory, aligned to eight bytes, or NULL if the arena
 * is too small. The memory is not zeroed. */
void *app_alloc(int32_t bytes);

/* Returns the number of bytes left in the arena. */
int32_t app_alloc_left(void);

/* app_alloc_mark() returns the current position of the arena.
 * app_alloc_release(mark) releases everything allocated after the mark.
 * Marks taken after a released mark become invalid. */
int32_t app_alloc_mark(void);
void app_alloc_release(int32_t mark);
