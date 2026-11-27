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
    uint16_t start, end, decoded;
    uint8_t key_length;
    bool escaped;
} selected_field_t;
typedef struct {
    const host_xml_field_t *fields;
    selected_field_t *selected;
    int field_count;
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
static char entity(const char **p, const char *end);
/* Every query rejects duplicate keys even on nodes the
 * consumer never queries. Hashes select candidates; bytes decide equality. */
enum { ATTRIBUTE_SLOTS = 64 };
static bool unique_attribute(uint16_t *slots, const char *doc, const char *key, const char *end)
{
    unsigned hash = 0;
    for (const char *s = key; s < end; s++)
        hash = hash * 31u + (unsigned char)*s;
    for (int n = 0; n < ATTRIBUTE_SLOTS; n++) {
        unsigned at = (hash + (unsigned)n) % ATTRIBUTE_SLOTS;
        if (!slots[at]) {
            slots[at] = (uint16_t)(key - doc);
            return true;
        }
        const char *left = doc + slots[at], *right = key;
        while (right < end && *left == *right) {
            left++;
            right++;
        }
        if (right == end && !rest(*left))
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
    uint16_t name_slots[ATTRIBUTE_SLOTS];
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
        unsigned attribute_count = 0;
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
            const char *key_end = p;
            if (!attribute_count)
                memset(name_slots, 0, sizeof name_slots);
            if (++attribute_count > ATTRIBUTE_SLOTS || !unique_attribute(name_slots, doc, key, p))
                return false;
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
                if (*p == '&') {
                    p++;
                    if (!entity(&p, end))
                        return false;
                } else
                    p++;
            }
            if (p == end)
                return false;
            if (q->attribute && id == q->node) {
                if (q->field_count) {
                    for (int i = 0; i < q->field_count; i++) {
                        selected_field_t *slot = &q->selected[i];
                        if (key_end - key == slot->key_length &&
                            !memcmp(q->fields[i].name, key, (size_t)slot->key_length)) {
                            if (slot->start)
                                return false;
                            slot->start = (uint16_t)(value - doc);
                            slot->end = (uint16_t)(p - doc);
                            break;
                        }
                    }
                } else if (matches(q, key, key_end)) {
                    if (q->value)
                        return false;
                    q->value = value;
                    q->end = p;
                }
            }
            p++;
        }
    }
}
static bool overlaps(const char *a, int an, const char *b, int bn)
{
    if (an == 0 || bn == 0) {
        return false;
    }
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
bool xml_document_valid(const char *doc, int32_t length)
{
    query_t q = {0};
    return scan(doc, length, &q);
}
int32_t xml_query_child(const char *doc, int32_t length, int32_t parent, const char *key,
                        int32_t keysz, int32_t from)
{
    if (keysz < 0 || (keysz > 0 && !key)) {
        return HOST_ERR_ARGUMENT;
    }
    const char *terminator = keysz > 0 ? memchr(key, 0, (size_t)keysz) : NULL;
    if (terminator) {
        keysz = (int32_t)(terminator - key);
    }
    if (parent < -1 || parent >= MAX_NODES || from < 0 || from > MAX_NODES ||
        (keysz != 0 && !key_valid(key, keysz)))
        return HOST_ERR_ARGUMENT;
    query_t q = {.key = key, .key_length = keysz, .node = parent, .from = from,
                 .result = HOST_ERR_NOT_FOUND};
    return scan(doc, length, &q) ? q.result : HOST_ERR_ARGUMENT;
}
static int decoded_length(const char *value, const char *end, bool *escaped)
{
    /* Validate even discarded label bytes before changing application memory. */
    const char *cursor = value;
    int decoded = 0;
    *escaped = memchr(cursor, '&', (size_t)(end - cursor)) != NULL;
    if (*escaped) {
        while (cursor < end) {
            if (*cursor++ == '&' && !entity(&cursor, end))
                return -1;
            decoded++;
        }
    } else {
        decoded = (int)(end - cursor);
    }
    return decoded;
}

static int copy_value(const char *value, const char *end, int decoded, bool escaped, char *out,
                      int capacity)
{
    bool cut = decoded > capacity;
    int used = cut ? capacity : decoded;
    if (!escaped) {
        memcpy(out, value, (size_t)used);
    } else {
        const char *cursor = value;
        for (int index = 0; index < used; index++) {
            char character = *cursor++;
            out[index] = character == '&' ? entity(&cursor, end) : character;
        }
    }
    if (cut) {
        int limit = capacity >= 4 ? capacity - 3 : capacity;
        int begin = limit;
        while (begin > 0 && ((unsigned char)out[begin - 1] & 192) == 128) {
            --begin;
        }
        if (begin > 0) {
            unsigned char lead = (unsigned char)out[begin - 1];
            int width = lead < 128 ? 1 : lead < 224 ? 2 : lead < 240 ? 3 : 4;
            if (limit - (begin - 1) < width) {
                limit = begin - 1;
            }
        }
        used = limit;
        if (capacity >= 4) {
            memcpy(out + used, "...", 3);
            used += 3;
        }
    }
    return used;
}

int32_t xml_query_attr(const char *doc, int32_t length, int32_t node, const char *key,
                       int32_t keysz, int32_t flags, char *out, int32_t capacity)
{
    if (node < 0 || node >= MAX_NODES || !key || keysz < 0 || !out || capacity < 0 ||
        (flags & ~HOST_XML_LABEL) || length < 1 || length > MAX_BYTES ||
        overlaps(out, capacity, doc, length) || overlaps(out, capacity, key, keysz)) {
        return HOST_ERR_ARGUMENT;
    }
    const char *terminator = memchr(key, 0, (size_t)keysz);
    if (terminator) {
        keysz = (int32_t)(terminator - key);
    }
    if (!key_valid(key, keysz)) {
        return HOST_ERR_ARGUMENT;
    }
    query_t query = {.key = key, .key_length = keysz, .node = node, .attribute = true};
    if (!scan(doc, length, &query)) {
        return HOST_ERR_ARGUMENT;
    }
    if (!query.value) {
        return HOST_ERR_NOT_FOUND;
    }
    bool escaped;
    int decoded = decoded_length(query.value, query.end, &escaped);
    if (decoded < 0) {
        return HOST_ERR_ARGUMENT;
    }
    if (decoded > capacity && !(flags & HOST_XML_LABEL)) {
        return HOST_ERR_NO_SPACE;
    }
    return copy_value(query.value, query.end, decoded, escaped, out, capacity);
}

int32_t xml_query_attrs(const char *doc, int32_t length, int32_t node,
                        const host_xml_field_t *fields, int32_t count, char *out, int32_t capacity)
{
    if (!doc || length < 1 || length > MAX_BYTES || node < 0 || node >= MAX_NODES || !fields ||
        ((uintptr_t)fields % _Alignof(host_xml_field_t)) || count < 1 || count > 16 || !out ||
        capacity < 1)
        return HOST_ERR_ARGUMENT;
    int table_bytes = count * (int)sizeof *fields;
    if (overlaps(out, capacity, doc, length) ||
        overlaps(out, capacity, (const char *)fields, table_bytes) ||
        overlaps(doc, length, (const char *)fields, table_bytes))
        return HOST_ERR_ARGUMENT;
    selected_field_t selected[16] = {0};
    for (int i = 0; i < count; i++) {
        const host_xml_field_t *f = fields + i;
        const char *end = memchr(f->name, 0, sizeof f->name);
        if (!end || !key_valid(f->name, (int)(end - f->name)) || !f->capacity ||
            (int)f->offset + f->capacity > capacity ||
            (f->flags & ~(HOST_XML_LABEL | HOST_XML_REQUIRED)) || f->reserved[0] ||
            f->reserved[1] || f->reserved[2])
            return HOST_ERR_ARGUMENT;
        selected[i].key_length = (uint8_t)(end - f->name);
        for (int j = 0; j < i; j++) {
            const host_xml_field_t *prior = fields + j;
            if (!strcmp(f->name, prior->name) ||
                overlaps(out + f->offset, f->capacity, out + prior->offset, prior->capacity))
                return HOST_ERR_ARGUMENT;
        }
    }
    query_t q = {.fields = fields,
                 .selected = selected,
                 .field_count = count,
                 .node = node,
                 .attribute = true};
    if (!scan(doc, length, &q))
        return HOST_ERR_ARGUMENT;
    int mask = 0;
    for (int i = 0; i < count; i++) {
        const host_xml_field_t *f = fields + i;
        selected_field_t *v = selected + i;
        if ((f->flags & HOST_XML_REQUIRED) && v->start == v->end)
            return HOST_ERR_ARGUMENT;
        if (!v->start)
            continue;
        int decoded = decoded_length(doc + v->start, doc + v->end, &v->escaped);
        if (decoded < 0 || (decoded >= f->capacity && !(f->flags & HOST_XML_LABEL)))
            return HOST_ERR_ARGUMENT;
        v->decoded = (uint16_t)decoded;
        mask |= 1 << i;
    }
    for (int i = 0; i < count; i++) {
        const selected_field_t *v = selected + i;
        const host_xml_field_t *f = fields + i;
        int used = copy_value(doc + v->start, doc + v->end, v->decoded, v->escaped,
                              out + f->offset, f->capacity - 1);
        out[f->offset + used] = 0;
    }
    return mask;
}
