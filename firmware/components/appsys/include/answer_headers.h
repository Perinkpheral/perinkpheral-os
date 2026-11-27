/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "host_api.h"

/* One response, owned by the network request until delivery returns. */
#define ANSWER_HEADERS_BYTES 1024
#define ANSWER_HEADER_NAME_MAX 63
typedef struct answer_headers {
    char text[ANSWER_HEADERS_BYTES];
    uint16_t used;
    uint8_t overflow : 1;
    uint8_t type_seen : 1;
    uint8_t mime_format : 2;
    uint8_t head_response : 1;
} answer_headers_t;

void answer_headers_type(answer_headers_t *headers,const char *type);
int answer_format(int policy,const answer_headers_t *headers,const host_answer_info_t *info);
void answer_headers_reset(answer_headers_t *headers);
bool answer_header_name_valid(const char *name, size_t length);
void answer_headers_add(answer_headers_t *headers, const char *name, const char *value);
int32_t answer_headers_get(const answer_headers_t *headers, const char *name, size_t name_length,
                           int32_t occurrence, char *value, int32_t capacity);

/* Fixed response metadata is independent of aggregate header storage. */
/* Complete unsigned decimal seconds or IMF-fixdate GMT; surrounding SP/HTAB
 * is ignored. Invalid/expired dates and dates with an unknown clock yield 0.
 * Positive overflow saturates at INT32_MAX. Capture unix_time once per response. */
int32_t answer_retry_after(const char *value, int64_t unix_time);
int32_t answer_poll_cooldown(const host_answer_info_t *response);
void answer_info_header(host_answer_info_t *response, const char *name, const char *value,
                        int64_t unix_time);
