/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>

/* Complete duration text; -1 when invalid. The caller supplies the bound. */
static inline int32_t app_duration_seconds(const char *text, uint32_t maximum)
{
    uint32_t number = 0;
    int digits = 0;
    while (*text >= '0' && *text <= '9' && digits < 6) {
        number = number * 10 + (uint32_t)(*text++ - '0');
        digits++;
    }
    while (*text == ' ') {
        text++;
    }
    static const struct { const char *text; uint32_t seconds; } units[] = {
        {"", 1}, {"s", 1}, {"sec", 1}, {"second", 1}, {"seconds", 1},
        {"m", 60}, {"min", 60}, {"minute", 60}, {"minutes", 60},
        {"h", 3600}, {"hour", 3600}, {"hours", 3600},
        {"d", 86400}, {"day", 86400}, {"days", 86400},
    };
    for (unsigned index = 0; index < sizeof units / sizeof units[0]; index++) {
        const char *given = text, *expected = units[index].text;
        while (*given != '\0' && *given == *expected) { given++; expected++; }
        if (digits > 0 && *given == '\0' && *expected == '\0' && number <= maximum / units[index].seconds) {
            return (int32_t)(number * units[index].seconds);
        }
    }
    return -1;
}
