/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The application area: the one piece of memory an application lives in.
 * Its linear memory and everything the interpreter needs to run it - the
 * compiled code, the stack, its own records, contract scratch memory - are
 * taken from here and from nowhere else, so that the host's heap is the
 * same whichever application is on the screen, and an application that
 * needs more than there is fails to start, every time, instead of now and
 * then. A module declares the memory its stack and data take, at most one
 * standard page of 64 KB (d_m3MaxLinearMemoryPages); what it leaves is the
 * interpreter's. ABI 3 keeps at least 112 KB for this shared area; a
 * module within the linear-memory ceiling may still fail to fit. */
#define M3_AREA_BYTES (112 * 1024)
#if M3_AREA_BYTES < (112 * 1024)
#error "ABI 3 application area floor"
#endif

/* Takes the area from the heap, once, for as long as the device runs.
 * Call it first of all, while the heap is whole. False if unavailable. */
bool m3_area_start(void);

/* Whether the boot reservation is usable, not merely configured. */
bool m3_area_available(void);

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
