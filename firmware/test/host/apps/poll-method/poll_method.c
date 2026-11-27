/* SPDX-License-Identifier: MIT */
#include "host_api.h"

static int32_t picked;
static int32_t streamed;
static int32_t headed;

void app_on_load(void)
{
    kv_get("picked", 6, &picked, sizeof picked);
    kv_get("streamed", 8, &streamed, sizeof streamed);
    kv_get("headed", 6, &headed, sizeof headed);
}

void app_on_picked(int32_t request, int32_t status)
{
    char value[2];
    if (request == 2 && status == 204 && answer_value(0, value, sizeof value) == HOST_ERR_NOT_FOUND) {
        headed = 1;
        kv_set("headed", 6, &headed, sizeof headed);
        screen_redraw(HOST_REDRAW_NORMAL);
    }
    if (request == 0 && status == 200 && answer_item() == 0 &&
        answer_value(0, value, sizeof value) == 2 && value[0] == 'o' && value[1] == 'k') {
        picked = 1;
        kv_set("picked", 6, &picked, sizeof picked);
        screen_redraw(HOST_REDRAW_NORMAL);
    }
}

void app_on_data(int32_t request, const uint8_t *data, int32_t length, int32_t flags, int32_t status)
{
    char value[3] = {0};
    const char *doc = (const char *)data;
    if (request == 1 && status == 200 &&
        (flags & (HOST_DATA_FIRST | HOST_DATA_LAST)) == (HOST_DATA_FIRST | HOST_DATA_LAST) &&
        xml_child(doc, length, -1, "reply", 5, 0) == 0 &&
        xml_attr(doc, length, 0, "state", 5, 0, value, sizeof value) == 2 &&
        value[0] == 'o' && value[1] == 'k' && !value[2]) {
        streamed = 1;
        kv_set("streamed", 8, &streamed, sizeof streamed);
        screen_redraw(HOST_REDRAW_NORMAL);
    }
}

void app_on_draw(void)
{
    gfx_draw_text(20, 30, HOST_FONT_TEXT, picked ? "POST picked" : "Waiting pick", picked ? 11 : 12);
    gfx_draw_text(20, 110, HOST_FONT_TEXT, headed ? "HEAD picked" : "Waiting HEAD", headed ? 11 : 12);
    gfx_draw_text(20, 70, HOST_FONT_TEXT, streamed ? "POST streamed" : "Waiting stream", streamed ? 13 : 14);
}
