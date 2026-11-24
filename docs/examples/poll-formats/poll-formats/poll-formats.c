/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe */
#include "app_text.h"
#include "app_values.h"
#include "host_api.h"
#include "json_pick.h"

enum { AUTO, XML_POST, JSON_PICK, UNTYPED, POLLS };

static uint8_t document[1024];
static json_pick_t json;
static char values[POLLS][64];
static int32_t raw_bytes;
static bool registered;

void app_on_load(void)
{
    registered = document_buffer_set(document, sizeof document) == HOST_OK;
}

void app_on_document(int32_t request, int32_t format, int32_t status, int32_t length)
{
    if ((request != AUTO && request != XML_POST) || status != 200 || length <= 0) {
        return;
    }

    char title[64];
    const char *bytes = (const char *)document;
    if (format == HOST_FORMAT_XML) {
        int32_t root = xml_child(bytes, length, -1, "MediaContainer", 14, 0);
        int32_t track = root < 0 ? -1 : xml_child(bytes, length, root, "Track", 5, 0);
        if (track < 0 || xml_attr(bytes, length, track, "title", 5, title, sizeof title, 1) != 1) {
            return;
        }
    } else if (format == HOST_FORMAT_JSON && request == AUTO) {
        static const char *const paths[] = {"[0].NowPlayingItem.Name"};
        json_pick_init(&json, paths, 1);
        if (!json_pick_feed(&json, bytes, length) || !json_pick_finish(&json)) {
            return;
        }
        const char *name = json_pick_value(&json, 0);
        if (name == NULL) {
            return;
        }
        app_text_copy(title, sizeof title, name);
    } else {
        return;
    }
    app_text_copy(values[request], sizeof values[request], title);
    screen_redraw(HOST_REDRAW_NORMAL);
}

void app_on_picked(int32_t request, int32_t status)
{
    char title[64];
    if (request == JSON_PICK && status == 200 && app_value_text(0, title, sizeof title)) {
        app_text_copy(values[JSON_PICK], sizeof values[JSON_PICK], title);
        screen_redraw(HOST_REDRAW_NORMAL);
    }
}

void app_on_data(int32_t request, const uint8_t *data, int32_t length, int32_t flags, int32_t status)
{
    (void)data;
    if (request != UNTYPED || status != 200 || (flags & HOST_DATA_FAILED)) {
        return;
    }
    if (flags & HOST_DATA_FIRST) {
        raw_bytes = 0;
    }
    raw_bytes += length;
    if (flags & HOST_DATA_LAST) {
        app_text_t text;
        app_text_start(&text, values[UNTYPED], sizeof values[UNTYPED]);
        app_text_add_number(&text, raw_bytes);
        app_text_add(&text, " raw bytes");
        screen_redraw(HOST_REDRAW_NORMAL);
    }
}

void app_on_draw(void)
{
    if (!registered) {
        gfx_draw_text(16, 42, HOST_FONT_TEXT, "Buffer unavailable", 18);
        return;
    }
    static const char *const labels[] = {"Auto", "XML POST", "JSON pick", "Untyped"};
    for (int i = 0; i < POLLS; ++i) {
        app_text_t text;
        char line[96];
        app_text_start(&text, line, sizeof line);
        app_text_add(&text, labels[i]);
        app_text_add(&text, "  ");
        app_text_add(&text, values[i][0] ? values[i] : "Waiting");
        gfx_draw_text(16, 42 + i * 44, HOST_FONT_TEXT, line, app_text_length(line));
    }
}
