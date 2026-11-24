/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe */
#include "answer_headers.h"
#include "host_api.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>

void answer_headers_reset(answer_headers_t *headers)
{
    memset(headers, 0, sizeof *headers);
}

bool answer_header_name_valid(const char *name, size_t length)
{
    if (name == NULL || length == 0 || length > ANSWER_HEADER_NAME_MAX) {
        return false;
    }
    for (size_t i = 0; i < length; i++) {
        unsigned char letter = (unsigned char)name[i];
        bool alphanumeric = (letter >= 'a' && letter <= 'z') ||
                            (letter >= 'A' && letter <= 'Z') ||
                            (letter >= '0' && letter <= '9');
        if (!alphanumeric && (letter == 0 || strchr("!#$%&'*+-.^_`|~", letter) == NULL)) {
            return false;
        }
    }
    return true;
}

void answer_headers_add(answer_headers_t *headers, const char *name, const char *value)
{
    if (headers->overflow) {
        return;
    }
    size_t name_length = strnlen(name, ANSWER_HEADER_NAME_MAX + 1);
    size_t available = sizeof headers->text - headers->used;
    if (!answer_header_name_valid(name, name_length) || name_length + 2 > available) {
        headers->overflow = true;
        return;
    }
    size_t value_length = strnlen(value, available - name_length - 1);
    if (value_length > available - name_length - 2) {
        /* A partial header set cannot safely describe an authentication response. */
        headers->overflow = true;
        return;
    }
    memcpy(headers->text + headers->used, name, name_length + 1);
    headers->used += (uint16_t)(name_length + 1);
    memcpy(headers->text + headers->used, value, value_length + 1);
    headers->used += (uint16_t)(value_length + 1);
}

static unsigned char lowercase(unsigned char letter)
{
    return letter >= 'A' && letter <= 'Z' ? letter + ('a' - 'A') : letter;
}

static bool same_name(const char *saved, const char *name, size_t length)
{
    if (strlen(saved) != length) {
        return false;
    }
    for (size_t i = 0; i < length; i++) {
        if (lowercase((unsigned char)saved[i]) != lowercase((unsigned char)name[i])) {
            return false;
        }
    }
    return true;
}

int32_t answer_headers_get(const answer_headers_t *headers, const char *name, size_t name_length,
                           int32_t occurrence, char *value, int32_t capacity)
{
    if (!answer_header_name_valid(name, name_length) || occurrence < 0 || capacity < 0 ||
        (capacity > 0 && value == NULL)) {
        return HOST_ERR_ARGUMENT;
    }
    if (headers == NULL) {
        return HOST_ERR_NOT_FOUND;
    }
    if (headers->overflow) {
        return HOST_ERR_NO_SPACE;
    }
    size_t offset = 0;
    while (offset < headers->used) {
        const char *saved_name = headers->text + offset;
        offset += strlen(saved_name) + 1;
        const char *saved_value = headers->text + offset;
        size_t length = strlen(saved_value);
        offset += length + 1;
        if (same_name(saved_name, name, name_length) && occurrence-- == 0) {
            if (length > (size_t)capacity) {
                return HOST_ERR_NO_SPACE;
            }
            if (length > 0) {
                memcpy(value, saved_value, length);
            }
            return (int32_t)length;
        }
    }
    return HOST_ERR_NOT_FOUND;
}

/* The headers an application may ask about, and where each goes. */
void answer_info_header(host_answer_info_t *response, const char *name, const char *value)
{
    const struct {
        const char *name;
        char       *text;
        size_t      size;
    } KEPT[] = {
        { "Content-Type", response->content_type, sizeof response->content_type },
        { "ETag", response->etag, sizeof response->etag },
        { "Last-Modified", response->last_modified, sizeof response->last_modified },
        { "Location", response->location, sizeof response->location },
        { "Link", response->link, sizeof response->link },
    };
    for (size_t i = 0; i < sizeof KEPT / sizeof KEPT[0]; i++) {
        if (strcasecmp(name, KEPT[i].name) == 0) {
            size_t length = strlen(value);
            if (length >= KEPT[i].size) {
                length = KEPT[i].size - 1;
            }
            memcpy(KEPT[i].text, value, length);
            KEPT[i].text[length] = '\0';
            return;
        }
    }
    if (strcasecmp(name, "Retry-After") == 0) {
        /* Seconds; a date instead of them reads as 0, which is "none". */
        long seconds = strtol(value, NULL, 10);
        response->retry_after = seconds > 0 && seconds < INT32_MAX ? (int32_t)seconds : 0;
    }
}
