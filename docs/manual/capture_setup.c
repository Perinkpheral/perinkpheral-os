/* SPDX-License-Identifier: MIT
 * Run the real setup application's drawing code against the native graphics library.
 * The simulator has no system_info service; only that service's answers are fixtures.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfx.h"
#include "qrcodegen.h"
#include "app_text.h"

#define SCREEN_WIDTH 400
#define SCREEN_HEIGHT 300
static uint8_t frame[SCREEN_WIDTH * SCREEN_HEIGHT / 8];
static gfx_t surface;
static int is_russian;

static int32_t gfx_width(void) { return surface.w; }
static int32_t gfx_height(void) { return surface.h; }
static void screen_leave(void) { }
static int32_t setup_font_ascent(int32_t font_id)
{
    (void)font_id;
    return gfx_font_ascent(gfx_font_system());
}
static int32_t gfx_text_width(int32_t font_id, const char *text, int32_t length)
{
    (void)font_id;
    (void)length;
    return gfx_text_font_width(gfx_font_system(), text);
}
static int32_t gfx_draw_text(int32_t x, int32_t y, int32_t font_id,
                             const char *text, int32_t length)
{
    (void)font_id;
    (void)length;
    return gfx_text_font(&surface, x, y + setup_font_ascent(0), gfx_font_system(), text, true);
}
static int32_t gfx_draw_qr(int32_t x, int32_t y, int32_t cell, const char *text, int32_t length)
{
    (void)length;
    uint8_t modules[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
    uint8_t scratch[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
    if (!qrcodegen_encodeText(text, scratch, modules, qrcodegen_Ecc_MEDIUM,
                             1, 10, qrcodegen_Mask_AUTO, true)) {
        return -1;
    }
    int size = qrcodegen_getSize(modules);
    for (int row = 0; cell > 0 && row < size; row++) {
        for (int column = 0; column < size; column++) {
            if (qrcodegen_getModule(modules, column, row)) {
                gfx_fill(&surface, x + column * cell, y + row * cell, cell, cell, true);
            }
        }
    }
    return size;
}
static int32_t sys_info(int32_t what, char *text, int32_t capacity)
{
    const char *value = what == 1 ? "Perinkpheral-A7D2" : what == 3 ? "192.168.4.1" :
                        what == 8 ? "WIFI:T:nopass;S:Perinkpheral-A7D2;;" : "";
    int length = (int)strlen(value);
    if (length > capacity) {
        return -1;
    }
    memcpy(text, value, (size_t)length);
    return length;
}

/* The setup view only needs these three string-builder operations. */
void app_text_start(app_text_t *text, char *buffer, int32_t capacity)
{
    text->next = buffer;
    text->last = buffer + capacity - 1;
    *text->next = '\0';
}
void app_text_add(app_text_t *text, const char *more)
{
    while (*more != '\0' && text->next < text->last) {
        *text->next++ = *more++;
    }
    *text->next = '\0';
}
int32_t app_text_length(const char *text)
{
    return (int32_t)strlen(text);
}

#define gfx_font_ascent setup_font_ascent
#include "../../firmware/system/setup/setup.c"
#undef gfx_font_ascent

int main(int argc, char **argv)
{
    if (argc != 4) {
        return 2;
    }
    is_russian = strcmp(argv[1], "ru") == 0;
    if (is_russian) {
        uint8_t pack[64 * 1024];
        FILE *input = fopen(argv[2], "rb");
        if (input == NULL) { return 1; }
        size_t length = fread(pack, 1, sizeof pack, input);
        fclose(input);
        if (!gfx_pack_add(pack, length)) { return 1; }
        /* The pack is needed until the last glyph is drawn. */
        gfx_init(&surface, frame, SCREEN_WIDTH, SCREEN_HEIGHT);
        gfx_clear(&surface, false);
        app_on_draw();
    } else {
        gfx_init(&surface, frame, SCREEN_WIDTH, SCREEN_HEIGHT);
        gfx_clear(&surface, false);
        app_on_draw();
    }
    FILE *output = fopen(argv[3], "wb");
    if (output == NULL) { return 1; }
    fprintf(output, "P4\n%d %d\n", SCREEN_WIDTH, SCREEN_HEIGHT);
    for (size_t index = 0; index < sizeof frame; index++) {
        fputc((uint8_t)~frame[index], output);
    }
    return fclose(output) == 0 ? 0 : 1;
}
