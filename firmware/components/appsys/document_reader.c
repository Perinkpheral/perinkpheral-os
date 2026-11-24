/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe */
#include "document_reader.h"
#include "host/net.h"
#include "xml_query.h"
#include <string.h>

static int validate(int format, const uint8_t *data, int length)
{
    if (format == HOST_FORMAT_XML)
        return xml_document_valid((const char *)data, length) ? 0 : HOST_FAILED_NO_ANSWER;
    return document_json_valid(data, length) ? 0 : HOST_FAILED_NO_ANSWER;
}
bool document_reader_feed(document_reader_t *r, uint8_t *target, int cap, int request, int format,
                          int status, const uint8_t *piece, int n, int flags)
{
    if (flags & HOST_DATA_FIRST) {
        *r = (document_reader_t){.request = request,
                                 .format = format,
                                 .status = status,
                                 .active = format != HOST_FORMAT_RAW};
    }
    if (flags & HOST_DATA_FAILED) {
        if (!r->active || r->request != request)
            *r = (document_reader_t){.format = format, .request = request};
        r->active = false;
        r->used = 0;
        r->status = status;
        return r->format != HOST_FORMAT_RAW;
    }
    if (!r->active || r->request != request)
        return false;
    if (!r->error && r->status >= 200 && r->status < 300 && r->status != 204 && r->status != 205) {
        if (!target)
            r->error = HOST_FAILED_NOT_READY;
        else if (n < 0 || n > cap - r->used)
            r->error = HOST_FAILED_TOO_LARGE;
        else if (n) {
            memcpy(target + r->used, piece, (size_t)n);
            r->used += n;
        }
    }
    if (!(flags & HOST_DATA_LAST))
        return false;
    r->active = false;
    if (!r->error && r->status >= 200 && r->status < 300 && r->status != 204 && r->status != 205)
        r->error = validate(r->format, target, r->used);
    if (r->error) {
        r->status = r->error;
        r->used = 0;
    }
    return true;
}
