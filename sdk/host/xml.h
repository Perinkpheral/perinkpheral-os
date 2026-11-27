/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Queries over an application-owned, complete UTF-8 attribute-only document.
 */
#pragma once
#include "base.h"

/* Each call validates all current document bytes, including bytes after a NUL.
 * Node numbers are zero-based preorder ordinals, valid only for the same
 * document bytes. No pointers are retained.
 * One root, at most 12288 bytes, 256 elements, 16 levels and 64 attributes
 * per element. Duplicate attribute names or unknown entities anywhere
 * invalidate the document. Names are ASCII: a letter or underscore followed
 * by letters, digits, underscores, dots, colons or hyphens.
 * The optional XML 1.0 declaration, whitespace, opening and closing tags
 * and empty tags are supported. BOMs, text, DTDs, comments and numeric
 * character references (decimal or hexadecimal) are unsupported. Attribute
 * values accept amp, lt, gt, apos and quot; literal tabs, line feeds and
 * carriage returns in attributes are rejected rather than normalized.
 * Names are counted text: the first NUL within name_length ends the name.
 * Find the first direct child with ordinal >= from. Use parent -1 for the
 * root, or an empty name to match any name. Returns an ordinal,
 * HOST_ERR_NOT_FOUND if absent, or HOST_ERR_ARGUMENT for an invalid document
 * or argument. A valid parent ordinal need not exist in this document;
 * it then has no children. */
#define HOST_SINCE_xml_child "0.1.100"
HOST_IMPORT(xml_child, int32_t xml_child(const char *doc, int32_t length, int32_t parent,
                                         const char *name, int32_t name_length, int32_t from));

/* Get a single attribute into a buffer disjoint from the document and name.
 * Returns bytes copied, including 0 for a present empty value, without NUL.
 * Absence returns HOST_ERR_NOT_FOUND; insufficient capacity without
 * HOST_XML_LABEL returns HOST_ERR_NO_SPACE. Invalid documents, arguments,
 * nonexistent nodes and unknown flags return HOST_ERR_ARGUMENT.
 * Flags are 0 for an exact value or HOST_XML_LABEL for shortening between
 * UTF-8 characters, with three ASCII dots when capacity >= 4. Capacity 0
 * is valid: an empty value or a shortened label returns 0; a nonempty exact
 * value returns HOST_ERR_NO_SPACE. Absence and errors leave output untouched.
 * A duplicate attribute anywhere invalidates the entire document. */
#define HOST_SINCE_xml_attr "0.1.100"
HOST_IMPORT(xml_attr, int32_t xml_attr(const char *doc, int32_t length, int32_t node,
                                       const char *name, int32_t name_length, int32_t flags, char *out,
                                       int32_t capacity));

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
 * shortening rule of xml_attr with one byte reserved for NUL.
 * Returns HOST_ERR_ARGUMENT for invalid input, unknown flags, duplicate
 * projection names, a nonexistent node, a missing or empty REQUIRED field,
 * or an exact value that does not fit its field including NUL.
 * Document, table and output must be mutually
 * disjoint; the table must be aligned to 2 bytes. Invalid input, duplicate
 * keys, or an unsuccessful field leaves the entire output untouched. */
#define HOST_SINCE_xml_attrs "0.1.100"
HOST_IMPORT(xml_attrs, int32_t xml_attrs(const char *doc, int32_t length, int32_t node,
                                         const host_xml_field_t *fields, int32_t count,
                                         char *out, int32_t capacity));
