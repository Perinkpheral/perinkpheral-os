/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "host_api.h"

/* Identical vectors run through the native stand-in and the Wasm imports. */
static bool text_contract_cases(void)
{
    char output[HOST_UI_TEXT_LINES_MAX + 2];
    host_text_argument_t arguments[2] = {{0, "", 0}, {0, "é", 2}};
    if (text_fill("a\0ignored", 9, NULL, 0, output, sizeof output) != 1 || output[0] != 'a') {
        return false;
    }
    arguments[0].text = "b\0ignored";
    arguments[0].length = 9;
    if (text_fill("{0}!", 4, arguments, 1, output, sizeof output) != 2 ||
        output[0] != 'b' || output[1] != '!') {
        return false;
    }
    char pattern[HOST_TEXT_MAX + 4] = "{0}";
    for (int index = 3; index < 126; index++) {
        pattern[index] = 'a';
    }
    pattern[126] = (char)0xc3;
    pattern[127] = (char)0xa9;
    arguments[0].text = "";
    arguments[0].length = 0;
    if (text_fill(pattern, 128, arguments, 1, output, sizeof output) != 123) {
        return false;
    }
    char piece[126];
    for (int index = 0; index < 126; index++) {
        piece[index] = 'a';
    }
    arguments[0].text = piece;
    arguments[0].length = sizeof piece;
    output[126] = '#';
    if (text_fill("{0}{1}!", 7, arguments, 2, output, sizeof output) != 126 || output[126] != '#') {
        return false;
    }
    for (int index = 0; index < 126; index++) {
        if (output[index] != 'a') {
            return false;
        }
    }
    const char nul_text[] = "a\0ignored";
    if (gfx_text_width(HOST_FONT_TEXT, nul_text, sizeof nul_text - 1) !=
            gfx_text_width(HOST_FONT_TEXT, "a", 1) ||
        ui_text_width(HOST_UI_TEXT, nul_text, sizeof nul_text - 1) !=
            ui_text_width(HOST_UI_TEXT, "a", 1)) {
        return false;
    }
    int32_t length = gfx_fit_text(HOST_FONT_TEXT, pattern + 3, 125, 4096, output, sizeof output);
    if (length != 125) {
        return false;
    }
    char boundary[HOST_TEXT_MAX + 2];
    for (int index = 0; index < HOST_TEXT_MAX - 1; index++) {
        boundary[index] = 'a';
    }
    boundary[HOST_TEXT_MAX - 1] = (char)0xc3;
    boundary[HOST_TEXT_MAX] = (char)0xa9;
    length = gfx_fit_text(HOST_FONT_TEXT, boundary, HOST_TEXT_MAX + 1, 4096, output, sizeof output);
    if (length != HOST_TEXT_MAX - 1 ||
        gfx_text_width(HOST_FONT_TEXT, boundary, HOST_TEXT_MAX + 1) !=
            gfx_text_width(HOST_FONT_TEXT, boundary, HOST_TEXT_MAX - 1)) {
        return false;
    }
    arguments[0].text = boundary;
    arguments[0].length = HOST_TEXT_MAX + 1;
    output[HOST_TEXT_MAX - 1] = '#';
    if (text_fill("{0}!", 4, arguments, 1, output, sizeof output) != HOST_TEXT_MAX - 1 ||
        output[HOST_TEXT_MAX - 1] != '#') {
        return false;
    }
    char multiline[HOST_UI_TEXT_LINES_MAX + 1];
    for (int index = 0; index < (int)sizeof multiline; index++) {
        multiline[index] = 'a';
    }
    int32_t width = 64 * ui_text_width(HOST_UI_TEXT, "a", 1);
    if (ui_draw_text_lines(-4096, -4096, HOST_UI_TEXT, multiline, sizeof multiline, width, 8) != 8 ||
        ui_draw_text_lines(-4096, -4096, HOST_UI_TEXT, "a\0ignored", 9, width, 8) != 1) {
        return false;
    }
    const char xml[] = "<r x='ok' empty=''/>";
    output[2] = '#';
    if (xml_child(xml, sizeof xml - 1, -1, "r", 1, 0) != 0 ||
        xml_child(xml, sizeof xml - 1, 0, "r", 1, 0) != HOST_ERR_NOT_FOUND ||
        xml_attr(xml, sizeof xml - 1, 0, "x", 1, 0, output, 2) != 2 ||
        output[0] != 'o' || output[1] != 'k' || output[2] != '#' ||
        xml_attr(xml, sizeof xml - 1, 0, "x", 1, 0, output, 0) != HOST_ERR_NO_SPACE ||
        xml_attr(xml, sizeof xml - 1, 0, "empty", 5, 0, output, 0) != 0 ||
        xml_attr(xml, sizeof xml - 1, 0, "missing", 7, 0, output, 0) != HOST_ERR_NOT_FOUND ||
        xml_attr(xml, sizeof xml - 1, 0, "x", 1, HOST_XML_LABEL, output, 0) != 0) {
        return false;
    }
    static const uint8_t binary[] = {'a', 0, 0xc3, 0xa9, 'b'};
    if (kv_set("counted\0ignored", 15, binary, sizeof binary) != HOST_OK ||
        kv_get("counted", 7, output, sizeof output) != sizeof binary) {
        return false;
    }
    for (unsigned index = 0; index < sizeof binary; index++) {
        if ((uint8_t)output[index] != binary[index]) {
            return false;
        }
    }
    kv_set("counted", 7, binary, 0);
    for (int style = HOST_TIME_CLOCK; style <= HOST_TIME_WEEKDAY_SHORT; style++) {
        char full[64];
        int32_t full_length = time_text(1795255200LL, style, full, sizeof full);
        if (full_length < 0) {
            return false;
        }
        for (int capacity = 1; capacity <= full_length; capacity++) {
            output[capacity] = '#';
            int32_t copied = time_text(1795255200LL, style, output, capacity);
            int32_t expected = capacity;
            while (expected < full_length && expected > 0 &&
                   ((uint8_t)full[expected] & 0xc0) == 0x80) {
                expected--;
            }
            if (copied != expected || output[capacity] != '#') {
                return false;
            }
            for (int index = 0; index < copied; index++) {
                if (output[index] != full[index]) {
                    return false;
                }
            }
        }
    }
    return true;
}
