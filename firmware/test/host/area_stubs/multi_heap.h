/* SPDX-License-Identifier: MIT */
#pragma once

#include <stddef.h>

typedef void *multi_heap_handle_t;
typedef struct {
    size_t minimum_free_bytes;
} multi_heap_info_t;

multi_heap_handle_t multi_heap_register(void *memory, size_t size);
void multi_heap_get_info(multi_heap_handle_t heap, multi_heap_info_t *info);
void *multi_heap_malloc(multi_heap_handle_t heap, size_t size);
void multi_heap_free(multi_heap_handle_t heap, void *memory);
void *multi_heap_realloc(multi_heap_handle_t heap, void *memory, size_t size);
