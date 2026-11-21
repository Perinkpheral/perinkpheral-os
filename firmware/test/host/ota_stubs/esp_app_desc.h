// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

typedef struct {
    uint32_t prefix[4];
    char version[32];
    char project_name[32];
    uint8_t suffix[176];
} esp_app_desc_t;

const esp_app_desc_t *esp_app_get_description(void);
