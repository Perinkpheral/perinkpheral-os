/* SPDX-License-Identifier: MIT */
#include <stddef.h>
#include "host_api.h"

#define INITIAL_BYTES 65528
#define LIMIT_BYTES   65536

void app_on_load(void)
{
    if (__builtin_wasm_memory_size(0) != INITIAL_BYTES ||
        __builtin_wasm_memory_grow(0, 0) != INITIAL_BYTES ||
        __builtin_wasm_memory_grow(0, 7) != INITIAL_BYTES ||
        __builtin_wasm_memory_size(0) != LIMIT_BYTES - 1) {
        __builtin_trap();
    }
    volatile uint8_t *memory = (volatile uint8_t *)(uintptr_t)INITIAL_BYTES;
    for (size_t index = 0; index < 7; index++) {
        if (memory[index] != 0) {
            __builtin_trap();
        }
        memory[index] = (uint8_t)(index + 1);
    }
    if (__builtin_wasm_memory_grow(0, 1) != LIMIT_BYTES - 1 ||
        __builtin_wasm_memory_size(0) != LIMIT_BYTES ||
        memory[7] != 0 || memory[6] != 7 ||
        __builtin_wasm_memory_grow(0, 1) != (size_t)-1 ||
        __builtin_wasm_memory_grow(0, (size_t)-1) != (size_t)-1 ||
        __builtin_wasm_memory_grow(0, 0) != LIMIT_BYTES ||
        __builtin_wasm_memory_size(0) != LIMIT_BYTES) {
        __builtin_trap();
    }
    screen_redraw(HOST_REDRAW_NORMAL);
}
