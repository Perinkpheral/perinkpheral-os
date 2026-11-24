/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe */
#include "xml_query.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

enum { MAX_BYTES = 12288, MAX_NODES = 256, MAX_DEPTH = 16 };
typedef struct {
    uint16_t start, length, index;
} element_t;
typedef struct {
    const char *key;
    int key_length, node, from, result;
    const char *value, *end;
    bool attribute;
} query_t;

static bool space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
static bool first(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }
static bool rest(char c)
{
    return first(c) || (c >= '0' && c <= '9') || c == ':' || c == '.' || c == '-';
}
static bool key_valid(const char *s, int n)
{
    if (!s || n < 1 || n > MAX_BYTES || !first(*s))
        return false;
    for (int i = 1; i < n; i++)
        if (!rest(s[i]))
            return false;
    return true;
}
static void spaces(const char **p, const char *end)
{
    while (*p < end && space(**p))
        ++*p;
}
static bool take(const char **p, const char *end, char c)
{
    if (*p == end || **p != c)
        return false;
    ++*p;
    return true;
}
static bool name(const char **p, const char *end)
{
    if (*p == end || !first(**p))
        return false;
    do {
        ++*p;
    } while (*p < end && rest(**p));
    return true;
}
/* Validate UTF-8 scalars and XML 1.0 character ranges before labels can be
 * shortened. Most network XML is ASCII and takes the first branch. */
static bool valid_text(const char *s, const char *end)
{
    while (s < end) {
        unsigned a = (unsigned char)*s++;
        if (a < 128) {
            if (a < 32 && a != 9 && a != 10 && a != 13)
                return false;
            continue;
        }
        int n = a < 224 ? 1 : a < 240 ? 2 : 3;
        if (a < 194 || a > 244 || end - s < n)
            return false;
        unsigned cp = a & (127u >> (n + 1));
        for (int i = 0; i < n; i++) {
            unsigned b = (unsigned char)*s++;
            if ((b & 192) != 128)
                return false;
            cp = (cp << 6) | (b & 63);
        }
        if ((n == 2 && cp < 2048) || (n == 3 && cp < 65536) || cp > 0x10ffff ||
            (cp >= 0xd800 && cp <= 0xdfff) || cp == 0xfffe || cp == 0xffff)
            return false;
    }
    return true;
}
static bool matches(const query_t *q, const char *s, const char *end)
{
    return end - s == q->key_length && !memcmp(s, q->key, (size_t)q->key_length);
}
/* Parse the optional XML 1.0 declaration. The reader is UTF-8 only. */
static bool token(const char **p, const char *end, const char *word)
{
    size_t n = strlen(word);
    if (n > (size_t)(end - *p) || memcmp(*p, word, n) != 0)
        return false;
    *p += n;
    return true;
}
static bool declaration(const char **p, const char *end)
{
    static const char *const fields[] = {"version", "encoding", "standalone"};
    int next = 0;
    while (*p < end) {
        const char *before = *p;
        spaces(p, end);
        if (next && token(p, end, "?>"))
            return true;
        if (before == *p)
            return false;
        int field = next;
        while (field < 3 && !token(p, end, fields[field])) {
            if (!field)
                return false;
            field++;
        }
        if (field == 3)
            return false;
        next = field + 1;
        spaces(p, end);
        if (!take(p, end, '='))
            return false;
        spaces(p, end);
        if (*p == end || (**p != 39 && **p != 34))
            return false;
        char quote = *(*p)++;
        if (field == 1) {
            static const char encoding[] = "utf-8";
            for (size_t i = 0; i < sizeof encoding - 1; i++) {
                if (*p == end)
                    return false;
                char c = *(*p)++;
                if (c >= 'A' && c <= 'Z')
                    c += 'a' - 'A';
                if (c != encoding[i])
                    return false;
            }
        } else if (field == 0 ? !token(p, end, "1.0")
                              : (!token(p, end, "yes") && !token(p, end, "no"))) {
            return false;
        }
        if (!take(p, end, quote))
            return false;
    }
    return false;
}
static bool scan(const char *doc, int length, query_t *q)
{
    if (!doc || length <= 0 || length > MAX_BYTES)
        return false;
    const char *p = doc, *end = doc + length;
    if (!valid_text(p, end))
        return false;
    element_t stack[MAX_DEPTH];
    int depth = 0, count = 0;
    spaces(&p, end);
    if (token(&p, end, "<?xml") && !declaration(&p, end))
        return false;
    for (;;) {
        spaces(&p, end);
        if (p == end)
            return count > 0 && depth == 0 && (!q->attribute || q->node < count);
        if (!take(&p, end, '<'))
            return false;
        bool closing = take(&p, end, '/');
        const char *tag = p;
        if (!name(&p, end))
            return false;
        int tag_length = (int)(p - tag);
        if (closing) {
            if (!depth)
                return false;
            element_t opened = stack[--depth];
            if (opened.length != tag_length || memcmp(doc + opened.start, tag, (size_t)tag_length))
                return false;
            spaces(&p, end);
            if (!take(&p, end, '>'))
                return false;
            continue;
        }
        if (count == MAX_NODES || depth == MAX_DEPTH || (!depth && count))
            return false;
        int id = count++, parent = depth ? stack[depth - 1].index : -1;
        if (!q->attribute && q->result < 0 && parent == q->node && id >= q->from &&
            (!q->key_length || matches(q, tag, p)))
            q->result = id;
        for (;;) {
            const char *before = p;
            spaces(&p, end);
            if (take(&p, end, '>')) {
                stack[depth++] =
                    (element_t){(uint16_t)(tag - doc), (uint16_t)tag_length, (uint16_t)id};
                break;
            }
            if (take(&p, end, '/')) {
                if (!take(&p, end, '>'))
                    return false;
                break;
            }
            const char *key = p;
            if (before == p || !name(&p, end))
                return false;
            bool wanted = q->attribute && id == q->node && matches(q, key, p);
            spaces(&p, end);
            if (!take(&p, end, '='))
                return false;
            spaces(&p, end);
            if (p == end || (*p != 39 && *p != 34))
                return false;
            char quote = *p++;
            const char *value = p;
            while (p < end && *p != quote) {
                if (*p == '<' || (unsigned char)*p < 32)
                    return false;
                p++;
            }
            if (p == end)
                return false;
            if (wanted) {
                if (q->value)
                    return false;
                q->value = value;
                q->end = p;
            }
            p++;
        }
    }
}
static bool overlaps(const char *a, int an, const char *b, int bn)
{
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x <= y ? y - x < (uintptr_t)an : x - y < (uintptr_t)bn;
}
static char entity(const char **p, const char *end)
{
    static const char *const names[] = {"amp;", "lt;", "gt;", "apos;", "quot;"};
    static const unsigned char lengths[] = {4, 3, 3, 5, 5};
    static const char values[] = {'&', '<', '>', 39, 34};
    for (unsigned i = 0; i < sizeof lengths; i++) {
        int n = lengths[i];
        if (end - *p >= n && !memcmp(*p, names[i], n)) {
            *p += n;
            return values[i];
        }
    }
    return 0;
}
int32_t xml_query_child(const char *doc, int32_t length, int32_t parent, const char *key,
                        int32_t keysz, int32_t from)
{
    if (parent < -1 || parent >= MAX_NODES || from < 0 || from > MAX_NODES ||
        (keysz != 0 && !key_valid(key, keysz)))
        return -2;
    query_t q = {.key = key, .key_length = keysz, .node = parent, .from = from, .result = -1};
    return scan(doc, length, &q) ? q.result : -2;
}
int32_t xml_query_attr(const char *doc, int32_t length, int32_t node, const char *key,
                       int32_t keysz, char *out, int32_t cap, int32_t label)
{
    if (node < 0 || node >= MAX_NODES || !key_valid(key, keysz) || !out || cap < 1 ||
        (label != 0 && label != 1) || length < 1 || length > MAX_BYTES ||
        overlaps(out, cap, doc, length) || overlaps(out, cap, key, keysz))
        return -1;
    query_t q = {.key = key, .key_length = keysz, .node = node, .attribute = true};
    if (!scan(doc, length, &q))
        return -1;
    if (!q.value)
        return 0;
    /* Validate even discarded label bytes before changing guest memory. */
    const char *cursor = q.value;
    int decoded = 0;
    bool escaped = memchr(cursor, '&', (size_t)(q.end - cursor)) != NULL;
    if (escaped) {
        while (cursor < q.end) {
            if (*cursor++ == '&' && !entity(&cursor, q.end))
                return -1;
            decoded++;
        }
    } else {
        decoded = (int)(q.end - cursor);
    }
    bool cut = decoded >= cap;
    if (cut && !label)
        return -1;
    int used = cut ? cap - 1 : decoded;
    if (!escaped) {
        memcpy(out, q.value, (size_t)used);
    } else {
        cursor = q.value;
        for (int i = 0; i < used; i++) {
            char c = *cursor++;
            out[i] = c == '&' ? entity(&cursor, q.end) : c;
        }
    }
    if (cut) {
        int limit = cap >= 5 ? cap - 4 : cap - 1;
        /* Bytes at or beyond limit may already be excluded. If the prefix
         * ends inside a scalar, remove the whole incomplete character. */
        int begin = limit;
        while (begin > 0 && ((unsigned char)out[begin - 1] & 192) == 128)
            --begin;
        if (begin > 0) {
            unsigned char lead = (unsigned char)out[begin - 1];
            int width = lead < 128 ? 1 : lead < 224 ? 2 : lead < 240 ? 3 : 4;
            if (limit - (begin - 1) < width)
                limit = begin - 1;
        }
        used = limit;
        if (cap >= 5) {
            memcpy(out + used, "...", 3);
            used += 3;
        }
    }
    out[used] = 0;
    return 1;
}
