/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Queries over an application-owned, complete UTF-8 attribute-only document.
 */
#pragma once
#include "base.h"

/* Each call validates the current document. Node numbers are zero-based
 * preorder ordinals, valid only for the same bytes. No pointers are retained.
 * One root, at most 12288 bytes, 256 elements, 16 levels and 64 attributes
 * per element. Duplicate attribute names within an element, or unknown
 * entities anywhere, invalidate the document. Names are ASCII:
 * a letter or underscore followed by letters, digits, underscores, dots,
 * colons or hyphens. The optional XML declaration, whitespace, opening and
 * closing tags and empty tags are supported. Text, DTDs and comments are
 * outside this subset. Names and documents are counted, not NUL-terminated.
 * Find the first direct child with ordinal >= from. Use parent -1 for the
 * root, or a zero-length name to match any name. Returns an ordinal, -1 if
 * absent, or -2 for an invalid document or argument. */
#define HOST_SINCE_xml_child "0.1.100"
HOST_IMPORT(xml_child, int32_t xml_child(const char *doc, int32_t length, int32_t parent,
                                         const char *name, int32_t name_length, int32_t from));

/* Get a single attribute into a disjoint buffer, NUL included in capacity.
 * Found (even empty) returns 1, absence 0, any error -1. A duplicate of the
 * requested attribute is ambiguous. Supports amp, lt, gt, apos and quot.
 * When label is 0 an
 * oversized value fails. With label 1 it is shortened on a UTF-8 boundary,
 * adding three ASCII dots if capacity >= 5. An absent value or error leaves the output untouched. */
#define HOST_SINCE_xml_attr "0.1.100"
HOST_IMPORT(xml_attr, int32_t xml_attr(const char *doc, int32_t length, int32_t node,
                                       const char *name, int32_t name_length, char *out,
                                       int32_t capacity, int32_t label));

/* A constant projection, shared by Wasm and native SDK checks. Names must be
 * NUL-terminated within 32 bytes. Offsets are relative to the output buffer.
 * Each capacity includes its terminator. Use distinct names and disjoint
 * output ranges. Reserved bytes must be zero. */
#define HOST_XML_LABEL 1
#define HOST_XML_REQUIRED 2
#define HOST_SINCE_HOST_XML_LABEL "0.1.100"
#define HOST_SINCE_HOST_XML_REQUIRED "0.1.100"
typedef struct host_xml_field {
    char name[32];
    uint16_t offset;
    uint16_t capacity;
    uint8_t flags;
    uint8_t reserved[3];
} host_xml_field_t;
HOST_LAYOUT(host_xml_field_t, 40);

/* Scan once for 1..16 fields of the same node. Returns bit i for every
 * present field, including an empty value. Missing optional fields produce
 * empty strings. REQUIRED demands a present nonempty value. LABEL uses the
 * shortening rule of xml_attr. Document, table and output must be mutually
 * disjoint; the table must be aligned to 2 bytes. Invalid input, duplicate
 * keys, or an unsuccessful field leaves the entire output untouched. */
#define HOST_SINCE_xml_attrs "0.1.100"
HOST_IMPORT(xml_attrs, int32_t xml_attrs(const char *doc, int32_t length, int32_t node,
                                         const host_xml_field_t *fields, int32_t count,
                                         char *out, int32_t capacity));
