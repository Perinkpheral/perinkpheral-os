/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe */
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    int request, format, status, used, error;
    bool active;
} document_reader_t;
bool document_json_valid(const uint8_t *data, int length);
/* Return true exactly when a structured result (possibly failed) is ready. */
bool document_reader_feed(document_reader_t *reader, uint8_t *target, int capacity, int request,
                          int format, int status, const uint8_t *piece, int length, int flags,
                          bool head);
