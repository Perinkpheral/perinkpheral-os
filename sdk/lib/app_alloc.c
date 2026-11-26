/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_alloc.h"

#include <stddef.h>

#define ALIGNMENT 8

/* The application's own: APP_ALLOC_ARENA(bytes). */
extern unsigned char app_alloc_arena[];
extern const int32_t app_alloc_arena_bytes;

static int32_t taken;

int32_t app_alloc_left(void)
{
    return app_alloc_arena_bytes - taken;
}

void *app_alloc(int32_t bytes)
{
    if (bytes <= 0 || bytes > app_alloc_left()) {
        return NULL;
    }
    void *piece = app_alloc_arena + taken;
    taken += (bytes + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
    return piece;
}

int32_t app_alloc_mark(void)
{
    return taken;
}

void app_alloc_release(int32_t mark)
{
    if (mark >= 0 && mark < taken) {
        taken = mark;
    }
}
