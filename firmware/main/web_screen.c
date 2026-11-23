/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "web_screen.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "display.h"
#include "events.h"
#include "host.h"
#include "units.h"

#define FRAME_BYTES     (DISPLAY_WIDTH / 8 * DISPLAY_HEIGHT)
#define NUMBER_TEXT_MAX 12

/* The frame is copied on the host loop, which draws it: never half of one frame and half of
 * the next. */
typedef struct {
    uint8_t  *frame; /* FRAME_BYTES */
    uint32_t  frames_shown;
    bool      frame_pending;
} screen_look_t;

static void look_at_screen(void *argument)
{
    screen_look_t *look = argument;
    memcpy(look->frame, display_shown_frame(), FRAME_BYTES);
    look->frames_shown = host_frames_shown();
    look->frame_pending = host_frame_is_pending();
}

/* A 1-bit BMP: two colours, rows from the bottom up, each padded to four
 * bytes. The frame's own bits fit as they are: 0 is ink, 1 is paper. */
#define BMP_HEADER_BYTES 62
#define BMP_ROW_BYTES    ((DISPLAY_WIDTH / 8 + 3) / 4 * 4)

/* Where the fields of the file's header are, in bytes from its start. */
enum {
    BMP_AT_FILE_BYTES = 2,
    BMP_AT_PIXELS_START = 10,
    BMP_AT_INFO_BYTES = 14,
    BMP_AT_WIDTH = 18,
    BMP_AT_HEIGHT = 22,
    BMP_AT_PLANES = 26,
    BMP_AT_BITS_PER_PIXEL = 28,
    BMP_AT_PIXELS_BYTES = 34,
    BMP_AT_COLOURS = 46,
    BMP_AT_PALETTE = 54,
};
#define BMP_INFO_BYTES 40
#define BMP_COLOURS    2
#define BMP_INK        0x00141413 /* the palette of the project: black #141413 */
#define BMP_PAPER      0x00E9E7E1 /* and paper #E9E7E1 */

static void put_u32(uint8_t *bytes, uint32_t value)
{
    for (size_t i = 0; i < sizeof value; i++) {
        bytes[i] = (uint8_t)(value >> (BITS_PER_BYTE * i));
    }
}

static void bmp_header(uint8_t header[BMP_HEADER_BYTES])
{
    memset(header, 0, BMP_HEADER_BYTES);
    header[0] = 'B';
    header[1] = 'M';
    put_u32(header + BMP_AT_FILE_BYTES, BMP_HEADER_BYTES + BMP_ROW_BYTES * DISPLAY_HEIGHT);
    put_u32(header + BMP_AT_PIXELS_START, BMP_HEADER_BYTES);
    put_u32(header + BMP_AT_INFO_BYTES, BMP_INFO_BYTES);
    put_u32(header + BMP_AT_WIDTH, DISPLAY_WIDTH);
    put_u32(header + BMP_AT_HEIGHT, DISPLAY_HEIGHT);
    header[BMP_AT_PLANES] = 1;
    header[BMP_AT_BITS_PER_PIXEL] = 1;
    put_u32(header + BMP_AT_PIXELS_BYTES, BMP_ROW_BYTES * DISPLAY_HEIGHT);
    put_u32(header + BMP_AT_COLOURS, BMP_COLOURS);
    put_u32(header + BMP_AT_PALETTE, BMP_INK);
    put_u32(header + BMP_AT_PALETTE + sizeof(uint32_t), BMP_PAPER);
}

static esp_err_t handle_screen(httpd_req_t *http)
{
    screen_look_t look = { .frame = malloc(FRAME_BYTES) };
    if (look.frame == NULL) {
        return web_send_text(http, "503 Service Unavailable", "no memory");
    }
    if (!events_call(look_at_screen, &look)) {
        free(look.frame);
        return web_send_text(http, "503 Service Unavailable", "the host did not answer");
    }
    char frames_shown[NUMBER_TEXT_MAX];
    snprintf(frames_shown, sizeof frames_shown, "%u", (unsigned)look.frames_shown);
    httpd_resp_set_type(http, "image/bmp");
    httpd_resp_set_hdr(http, "X-Frame", frames_shown);
    httpd_resp_set_hdr(http, "X-Pending", look.frame_pending ? "1" : "0");
    httpd_resp_set_hdr(http, "Cache-Control", "no-store");

    uint8_t header[BMP_HEADER_BYTES];
    bmp_header(header);
    esp_err_t err = httpd_resp_send_chunk(http, (const char *)header, sizeof header);
    const int frame_row_bytes = DISPLAY_WIDTH / 8;
    for (int row = DISPLAY_HEIGHT - 1; row >= 0 && err == ESP_OK; row--) {
        uint8_t padded[BMP_ROW_BYTES] = { 0 };
        memcpy(padded, look.frame + row * frame_row_bytes, (size_t)frame_row_bytes);
        err = httpd_resp_send_chunk(http, (const char *)padded, sizeof padded);
    }
    free(look.frame);
    return err == ESP_OK ? httpd_resp_send_chunk(http, NULL, 0) : err;
}

const web_route_t WEB_SCREEN_ROUTES[] = {
    { "/api/screen", HTTP_GET, handle_screen, WEB_NO_MEMORY_NEED, WEB_OWN_PAGE, WEB_AT_ONCE },
};
const size_t WEB_SCREEN_ROUTE_COUNT = WEB_ROUTE_COUNT(WEB_SCREEN_ROUTES);
