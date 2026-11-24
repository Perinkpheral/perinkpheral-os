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
 * One root, at most 12288 bytes, 256 elements and 16 levels. Names are ASCII:
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
 * Unknown references in a requested value are errors. When label is 0 an
 * oversized value fails. With label 1 it is shortened on a UTF-8 boundary,
 * adding three ASCII dots if capacity >= 5. An absent value or error leaves the output untouched. */
#define HOST_SINCE_xml_attr "0.1.100"
HOST_IMPORT(xml_attr, int32_t xml_attr(const char *doc, int32_t length, int32_t node,
                                       const char *name, int32_t name_length, char *out,
                                       int32_t capacity, int32_t label));
