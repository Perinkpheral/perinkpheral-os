/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe */
#pragma once
#include "host/net.h"
#include <string.h>

static inline int response_word(const char *s, size_t n, const char *word)
{
    if (strlen(word) != n)
        return 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
        if (word[i] != c)
            return 0;
    }
    return 1;
}
static inline int response_format(int policy, const char *type)
{
    if (policy)
        return policy == 3 ? HOST_FORMAT_RAW : policy;
    if (!type)
        return HOST_FORMAT_RAW;
    while (*type == ' ' || *type == '\t')
        type++;
    const char *end = type + strcspn(type, ";");
    while (end > type && (end[-1] == ' ' || end[-1] == '\t'))
        end--;
    const char *slash = memchr(type, '/', (size_t)(end - type));
    if (!slash || slash == type || slash + 1 == end)
        return HOST_FORMAT_RAW;
    for (const char *p = type; p < end; p++) {
        unsigned char c = (unsigned char)*p;
        if (p != slash && !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                            (c >= '0' && c <= '9') || strchr("!#$%&'*+-.^_`|~", c)))
            return HOST_FORMAT_RAW;
    }
    size_t n = (size_t)(end - slash - 1);
    int standard = response_word(type, slash - type, "application") ||
                   response_word(type, slash - type, "text");
    if ((standard && response_word(slash + 1, n, "json")) ||
        (n > 5 && response_word(end - 5, 5, "+json")))
        return HOST_FORMAT_JSON;
    if ((standard && response_word(slash + 1, n, "xml")) ||
        (n > 4 && response_word(end - 4, 4, "+xml")))
        return HOST_FORMAT_XML;
    return HOST_FORMAT_RAW;
}
