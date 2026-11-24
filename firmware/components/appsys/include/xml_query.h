/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe */
#pragma once
#include "host/xml.h"
int32_t xml_query_child(const char *doc, int32_t length, int32_t parent, const char *name,
                        int32_t name_length, int32_t from);
int32_t xml_query_attr(const char *doc, int32_t length, int32_t node, const char *name,
                       int32_t name_length, char *out, int32_t capacity, int32_t label);

int32_t xml_query_attrs(const char *doc, int32_t length, int32_t node,
                        const host_xml_field_t *fields, int32_t count, char *out, int32_t capacity);
