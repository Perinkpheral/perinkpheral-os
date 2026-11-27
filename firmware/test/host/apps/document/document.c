/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe */
#include "host_api.h"

static uint8_t document[8];
static int32_t documents;
#ifdef DUAL_HANDLER
static uint8_t piece[HOST_DATA_CHUNK_MAX];
static int32_t pieces;
uint8_t *app_data_buffer(void) { return piece; }
void app_on_data(int32_t request, const uint8_t *data, int32_t length, int32_t flags, int32_t status)
{
    (void)data;
    int32_t result[] = { ++pieces, request, length, flags, status };
    kv_set("piece", 5, result, sizeof result);
}
#endif

void app_on_load(void)
{
    uint8_t no_buffer = 0;
    kv_get("no_buffer", 9, &no_buffer, sizeof no_buffer);
    if (!no_buffer)
        document_buffer_set(document, sizeof document);
}

void app_on_document(int32_t request, int32_t format, int32_t status, int32_t length)
{
    int32_t result[] = { ++documents, request, format, status, length };
    kv_set("document", 8, result, sizeof result);
    host_answer_info_t info = {0};
    int32_t metadata[] = { answer_info(&info, sizeof info), info.status, answer_item(),
        document_buffer_set(document, sizeof document) };
    kv_set("metadata", 8, metadata, sizeof metadata);
}
