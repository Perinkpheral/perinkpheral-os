/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Test fixture. The host can split any token across response pieces. Only a
 * complete response belongs to the XML reader; failed requests discard it.
 */
#include "host_api.h"
#include "app_text.h"

#include <stdbool.h>

enum {
    POLL_GET,
    POLL_POST,
    JSON_PICK,
    POLL_GET_BAD,
    POLL_POST_BAD,
    XML_PICK,
    GET_OK,
    POST_BAD,
    POST_OK,
    GET_BAD,
    GET_FAILED,
    POST_HTTP_ERROR,
    GET_RECOVERED,
    CASE_COUNT
};
static const char *const paths[] = {"get",     "post-bad",   "post", "get-bad",
                                    "offline", "http-error", "again"};
static const char *const label[] = {"Poll GET",      "Poll POST",      "JSON pick",  "Poll GET bad",
                                    "Poll POST bad", "XML pick fails", "GET XML",    "POST invalid",
                                    "POST XML",      "GET truncated",  "GET failed", "POST 503",
                                    "GET again"};
static const int methods[] = {HOST_HTTP_GET, HOST_HTTP_POST, HOST_HTTP_POST, HOST_HTTP_GET,
                              HOST_HTTP_GET, HOST_HTTP_POST, HOST_HTTP_GET};
static int seen, cursor;
static uint8_t stream[2048];
static int owner = -1, used, chunks, first_status;
static bool picked_only = true, negative;

static void pass(int id)
{
    seen |= 1 << id;
    kv_set("seen", 4, &seen, sizeof seen);
    screen_redraw(HOST_REDRAW_NORMAL);
}
void app_on_load(void)
{
    char mode;
    negative = kv_get("neg", 3, &mode, 1) == 1 && mode == '1';
    kv_get("seen", 4, &seen, sizeof seen);
}
void app_on_button(int32_t event)
{
    if (event != BTN_OK_PRESSED || cursor >= CASE_COUNT - GET_OK)
        return;
    char url[96] = "https://example.test/";
    int index = cursor++;
    int prefix = app_text_length(url);
    app_text_copy(url + prefix, sizeof url - prefix, paths[index]);
    const char *body = methods[index] == HOST_HTTP_POST ? "filter=one" : "";
    if (http_send(HOST_REQUEST_FIRST + index, methods[index], url, app_text_length(url), "", 0,
                  (const uint8_t *)body, app_text_length(body)) != HOST_OK)
        screen_redraw(HOST_REDRAW_NORMAL);
}
void app_on_picked(int32_t request, int32_t status)
{
    if (negative && request == JSON_PICK)
        request = XML_PICK;
    if (request == XML_PICK && status == HOST_FAILED_NO_ANSWER) {
        pass(XML_PICK);
        return;
    }
    char value[8];
    if (request == JSON_PICK && picked_only && status == 200 &&
        answer_value(0, value, sizeof value) == 4 && value[0] == 'j' && value[1] == 's' &&
        value[2] == 'o' && value[3] == 'n')
        pass(JSON_PICK);
}
void app_on_data(int32_t request, const uint8_t *data, int32_t length, int32_t flags,
                 int32_t status)
{
    int id = request >= HOST_REQUEST_FIRST ? GET_OK + request - HOST_REQUEST_FIRST : request;
    if (negative && request >= 0 && request <= JSON_PICK)
        id = request + POLL_GET_BAD;
    if (id < 0 || id >= CASE_COUNT)
        return;
    if (id == JSON_PICK || id == XML_PICK) {
        picked_only = false;
        return;
    }
    if (flags & HOST_DATA_FAILED) {
        owner = -1;
        used = chunks = 0;
        if (id == GET_FAILED && status == HOST_FAILED_NO_ANSWER)
            pass(id);
        return;
    }
    if (flags & HOST_DATA_FIRST) {
        owner = id;
        used = chunks = 0;
        first_status = status;
    }
    if (owner != id || length < 0 || length > (int)sizeof stream - used) {
        owner = -1;
        return;
    }
    for (int i = 0; i < length; i++)
        stream[used + i] = data[i];
    used += length;
    chunks++;
    if (!(flags & HOST_DATA_LAST))
        return;
    owner = -1;
    if (id == POST_HTTP_ERROR) {
        if (first_status == 503)
            pass(id);
        return;
    }
    if (first_status != 200 || chunks < 2)
        return;
    const char *doc = (const char *)stream;
    int item = xml_child(doc, used, 0, "item", 4, 0);
    static const host_xml_field_t fields[] = {{"id", 0, 12, HOST_XML_REQUIRED, {0}},
                                              {"label", 12, 12, HOST_XML_REQUIRED, {0}}};
    char answer[24] = "untouched";
    int mask = xml_attrs(doc, used, 1, fields, 2, answer, sizeof answer);
    if (id == POST_BAD || id == GET_BAD || id == POLL_GET_BAD || id == POLL_POST_BAD) {
        if (item == HOST_ERR_ARGUMENT && mask == HOST_ERR_ARGUMENT && app_text_equal(answer, "untouched"))
            pass(id);
        return;
    }
    const char *expected = id == POLL_GET || id == GET_OK ? "get"
                           : id == GET_RECOVERED          ? "again"
                                                          : "post";
    if (item == 1 && mask == 3 && app_text_equal(answer, expected) &&
        app_text_equal(answer + 12, "A&B"))
        pass(id);
}
void app_on_draw(void)
{
    int row = 0;
    for (int i = 0; i < CASE_COUNT; i++) {
        if ((i >= POLL_GET_BAD && i <= XML_PICK) != negative)
            continue;
        const char *state = (seen & (1 << i)) ? "pass" : "waiting";
        int y = 21 + (row % 5) * 35, x = row < 5 ? 8 : 206;
        row++;
        gfx_draw_text(x, y, HOST_FONT_SMALL, label[i], app_text_length(label[i]));
        gfx_draw_text(x + 106, y, HOST_FONT_SMALL, state, app_text_length(state));
    }
}
