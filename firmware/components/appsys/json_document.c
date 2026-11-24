/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe */
#include "document_reader.h"
#include <stddef.h>
#include <string.h>

typedef struct {
    const uint8_t *at, *end;
} cursor_t;

static bool take(cursor_t *c, uint8_t token)
{
    if (c->at == c->end || *c->at != token)
        return false;
    c->at++;
    return true;
}
static int hex4(cursor_t *c)
{
    if (c->end - c->at < 4)
        return -1;
    int value = 0;
    for (int i = 0; i < 4; i++) {
        int ch = *c->at++;
        if (ch >= '0' && ch <= '9')
            ch -= '0';
        else if ((ch | 32) >= 'a' && (ch | 32) <= 'f')
            ch = (ch | 32) - 'a' + 10;
        else
            return -1;
        value = value * 16 + ch;
    }
    return value;
}
static bool escape(cursor_t *c)
{
    if (c->at == c->end)
        return false;
    int ch = *c->at++;
    if (ch != 'u')
        return ch && strchr("\"\\/bfnrt", ch) != NULL;
    int scalar = hex4(c);
    if (scalar < 0 || (scalar >= 0xdc00 && scalar <= 0xdfff))
        return false;
    if (scalar < 0xd800 || scalar > 0xdbff)
        return true;
    if (!take(c, '\\') || !take(c, 'u'))
        return false;
    int low = hex4(c);
    return low >= 0xdc00 && low <= 0xdfff;
}
static bool string(cursor_t *c)
{
    while (c->at < c->end) {
        unsigned ch = *c->at++;
        if (ch == '"')
            return true;
        if (ch < 32)
            return false;
        if (ch == '\\') {
            if (!escape(c))
                return false;
            continue;
        }
        if (ch < 128)
            continue;
        unsigned count = ch < 224 ? 1 : ch < 240 ? 2 : 3;
        if (ch < 194 || ch > 244 || (size_t)(c->end - c->at) < count)
            return false;
        unsigned scalar = ch & (127u >> (count + 1));
        for (unsigned i = 0; i < count; i++) {
            unsigned next = *c->at++;
            if ((next & 192) != 128)
                return false;
            scalar = (scalar << 6) | (next & 63);
        }
        if ((count == 2 && scalar < 2048) || (count == 3 && scalar < 65536) ||
            (scalar >= 0xd800 && scalar <= 0xdfff) || scalar > 0x10ffff)
            return false;
    }
    return false;
}
static bool digits(cursor_t *c)
{
    const uint8_t *start = c->at;
    while (c->at < c->end && *c->at >= '0' && *c->at <= '9')
        c->at++;
    return c->at != start;
}
static bool number(cursor_t *c)
{
    take(c, '-');
    if (!take(c, '0') && !digits(c))
        return false;
    if (take(c, '.') && !digits(c))
        return false;
    if (take(c, 'e') || take(c, 'E')) {
        if (!take(c, '-'))
            take(c, '+');
        if (!digits(c))
            return false;
    }
    return true;
}
static bool literal(cursor_t *c, const char *word)
{
    size_t length = strlen(word);
    if ((size_t)(c->end - c->at) < length || memcmp(c->at, word, length))
        return false;
    c->at += length;
    return true;
}
bool document_json_valid(const uint8_t *data, int length)
{
    if (!data || length <= 0)
        return false;
    enum {
        ROOT_VALUE,
        DONE,
        ARRAY_FIRST,
        ARRAY_VALUE,
        ARRAY_SEPARATOR,
        OBJECT_FIRST,
        OBJECT_KEY,
        OBJECT_COLON,
        OBJECT_VALUE,
        OBJECT_SEPARATOR
    };
    uint8_t state[65] = {ROOT_VALUE};
    int depth = 0;
    cursor_t c = {.at = data, .end = data + length};
    for (;;) {
        while (c.at < c.end && (*c.at == ' ' || *c.at == '\n' || *c.at == '\t' || *c.at == '\r'))
            c.at++;
        uint8_t here = state[depth];
        if (here == DONE)
            return c.at == c.end;
        if (c.at == c.end)
            return false;
        if (here == ARRAY_SEPARATOR || here == OBJECT_SEPARATOR) {
            if (take(&c, here == ARRAY_SEPARATOR ? ']' : '}')) {
                depth--;
                continue;
            }
            if (!take(&c, ','))
                return false;
            state[depth] = here == ARRAY_SEPARATOR ? ARRAY_VALUE : OBJECT_KEY;
            continue;
        }
        if (here == OBJECT_FIRST || here == OBJECT_KEY) {
            if (here == OBJECT_FIRST && take(&c, '}')) {
                depth--;
                continue;
            }
            if (!take(&c, '"') || !string(&c))
                return false;
            state[depth] = OBJECT_COLON;
            continue;
        }
        if (here == OBJECT_COLON) {
            if (!take(&c, ':'))
                return false;
            state[depth] = OBJECT_VALUE;
            continue;
        }
        if (here == ARRAY_FIRST && take(&c, ']')) {
            depth--;
            continue;
        }
        state[depth] = here == ROOT_VALUE     ? DONE
                       : here == OBJECT_VALUE ? OBJECT_SEPARATOR
                                              : ARRAY_SEPARATOR;
        uint8_t ch = *c.at;
        if (ch == '[' || ch == '{') {
            if (depth == 64)
                return false;
            state[++depth] = ch == '[' ? ARRAY_FIRST : OBJECT_FIRST;
            c.at++;
        } else if (ch == '"') {
            c.at++;
            if (!string(&c))
                return false;
        } else if (ch == 't' || ch == 'f' || ch == 'n') {
            if (!literal(&c, ch == 't' ? "true" : ch == 'f' ? "false" : "null"))
                return false;
        } else if (!number(&c))
            return false;
    }
}
