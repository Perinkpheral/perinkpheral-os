/* SPDX-License-Identifier: MIT */
#include <stddef.h>
#include "host_api.h"

#define INITIAL_BYTES 65528

void app_on_load(void)
{
    if (__builtin_wasm_memory_size(0) != INITIAL_BYTES ||
        __builtin_wasm_memory_grow(0, 0) != INITIAL_BYTES ||
        __builtin_wasm_memory_grow(0, 1) != (size_t)-1 ||
        __builtin_wasm_memory_grow(0, (size_t)-1) != (size_t)-1 ||
        __builtin_wasm_memory_size(0) != INITIAL_BYTES) {
        __builtin_trap();
    }
    screen_redraw(HOST_REDRAW_NORMAL);
}
