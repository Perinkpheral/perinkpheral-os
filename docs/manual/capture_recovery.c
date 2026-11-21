/* SPDX-License-Identifier: MIT
 * Render recovery with the same fictional release shown on the browser page.
 */
#include <stdio.h>
#include <string.h>
#include "screen.h"
#include "words.h"

#define SCREEN_WIDTH 400
#define SCREEN_HEIGHT 300
#define GLYPH_PACK_CAPACITY (64 * 1024)
static uint8_t frame[SCREEN_WIDTH * SCREEN_HEIGHT / 8];
static uint8_t glyph_pack[GLYPH_PACK_CAPACITY];

int main(int argc, char **argv)
{
    if (argc != 6) { return 2; }
    if (strcmp(argv[1], "ru") == 0) {
        FILE *input = fopen(argv[2], "rb");
        if (input == NULL) { return 1; }
        size_t length = fread(glyph_pack, 1, sizeof glyph_pack, input);
        bool whole = !ferror(input) && fgetc(input) == EOF;
        fclose(input);
        if (!whole || !gfx_pack_add(glyph_pack, length)) { return 1; }
        words_use("ru");
    }
    gfx_t graphics;
    gfx_init(&graphics, frame, SCREEN_WIDTH, SCREEN_HEIGHT);
    recovery_state_t state = strcmp(argv[3], "confirm") == 0 ? CONFIRM : WAITING;
    recovery_screen_draw(&graphics, state, argv[4], "", "perink-recovery", RECOVERY_WAIT_NETWORK);
    FILE *output = fopen(argv[5], "wb");
    if (output == NULL) { return 1; }
    fprintf(output, "P4\n%d %d\n", SCREEN_WIDTH, SCREEN_HEIGHT);
    for (size_t index = 0; index < sizeof frame; index++) {
        fputc((uint8_t)~frame[index], output);
    }
    return fclose(output) == 0 ? 0 : 1;
}
