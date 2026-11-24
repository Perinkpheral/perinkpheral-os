#ifndef FIXED
#define FIXED 0
#endif
/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe */
#include "host_api.h"
#include "app_text.h"
#include <stddef.h>

static uint8_t document[1100];
static int report[12], chunks[12], cursor;
static const char *const rows[] = {"JSON poll", "XML poll",  "Raw poll", "JSON pick",
                                   "GET",       "POST",      "Plain",    "No MIME",
                                   "Invalid",   "Too large", "Recover",  "HTTP error"};
static const char *const paths[] = {"get", "post",  "plain",   "missing",
                                    "bad", "large", "recover", "error"};
static int id_for(int request)
{
    return request >= HOST_REQUEST_FIRST ? request - HOST_REQUEST_FIRST + 4 : request;
}

void app_on_load(void)
{
    document_buffer_set(document, sizeof document);
    kv_get("rows", 4, report, sizeof report);
}
static void mark(int id, int result)
{
    if (id >= 0 && id < 12) {
        report[id] = result;
        kv_set("rows", 4, report, sizeof report);
        screen_redraw(HOST_REDRAW_NORMAL);
    }
}
void app_on_button(int32_t key)
{
    if (key != BTN_OK_PRESSED || cursor >= 8)
        return;
    int index = cursor++;
    char url[100] = "https://example.test/";
    int prefix = app_text_length(url);
    app_text_copy(url + prefix, sizeof url - prefix, paths[index]);
    int method = (index & 1) ? HOST_HTTP_POST : HOST_HTTP_GET;
    http_send(HOST_REQUEST_FIRST + index, method, url, app_text_length(url), "", 0,
              method == HOST_HTTP_POST ? (const uint8_t *)"one" : NULL,
              method == HOST_HTTP_POST ? 3 : 0);
}
void app_on_data(int32_t request, const uint8_t *data, int32_t length, int32_t flags,
                 int32_t status)
{
    int id = id_for(request);
    if (id < 0 || id >= 12)
        return;
    if (flags & HOST_DATA_FIRST)
        chunks[id] = 0;
    chunks[id]++;
    if (!(flags & HOST_DATA_LAST) || status != 200)
        return;
    if (id == 2 || (!FIXED && (id == 6 || id == 7))) {
        mark(id, length > 0 && data != NULL ? 1 : -1);
    }
    if (id == 3)
        mark(id, -1);
}
void app_on_picked(int32_t request, int32_t status)
{
    char value[4];
    if (request == 3)
        mark(3, status == 200 && answer_value(0, value, sizeof value) == 2 && value[0] == '4' &&
                        value[1] == '2'
                    ? 1
                    : -1);
}
void app_on_document(int32_t request, int32_t format, int32_t status, int32_t length)
{
    int id = id_for(request);
    if (id < 0 || id >= 12)
        return;
    int expected =
        (id == 1 || (!FIXED && (id == 5 || id == 10))) ? HOST_FORMAT_XML : HOST_FORMAT_JSON;
    if (id == 2 || id == 3 || (!FIXED && (id == 6 || id == 7)) || format != expected ||
        !chunks[id]) {
        mark(id, -1);
        return;
    }
    if (id == 8 || id == 9 || id == 11) {
        int failure = id == 8 ? HOST_FAILED_NO_ANSWER : id == 9 ? HOST_FAILED_TOO_LARGE : 503;
        mark(id, status == failure && length == 0 ? 1 : -1);
        return;
    }
    if (status != 200 || length <= 0 || (id == 0 && chunks[id] < 2)) {
        mark(id, -1);
        return;
    }
    if (format == HOST_FORMAT_XML) {
        char value[3];
        mark(id, xml_attr((const char *)document, length, 0, "a", 1, value, sizeof value, 0) == 1 &&
                         app_text_equal(value, "ok")
                     ? 1
                     : -1);
    } else {
        /* Consume the exact validated JSON bytes supplied to the module. */
        mark(id, document[0] == '{' && document[length - 1] == '}' ? 1 : -1);
    }
}
void app_on_draw(void)
{
    for (int i = 0; i < 12; i++) {
        const char *state = report[i] == 1 ? "pass" : report[i] < 0 ? "FAIL" : "waiting";
        int x = i < 6 ? 8 : 206, y = 18 + (i % 6) * 32;
        gfx_draw_text(x, y, HOST_FONT_SMALL, rows[i], app_text_length(rows[i]));
        gfx_draw_text(x + 100, y, HOST_FONT_SMALL, state, app_text_length(state));
    }
}
