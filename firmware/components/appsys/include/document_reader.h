/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe */
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    int request, format, status, used, error;
    bool active;
    bool ended; /* the result of `request` was given before its last piece */
} document_reader_t;
bool document_json_valid(const uint8_t *data, int length);
/* Return true exactly when a structured result (possibly failed) is ready. */
bool document_reader_feed(document_reader_t *reader, uint8_t *target, int capacity, int request,
                          int format, int status, const uint8_t *piece, int length, int flags,
                          bool head);
/* A document whose result is decided - failed (too large, no buffer), or
 * a refusal whose status is the whole result - is given now, before the
 * rest of the answer arrives: returns true when there is such a result
 * to give. The pieces of the same answer that still come, its failure
 * among them, give no second result. */
bool document_reader_end_early(document_reader_t *reader);
