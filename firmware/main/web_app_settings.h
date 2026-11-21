/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "json_tree.h"

bool web_app_settings_value(json_t *values, json_t *setting, const char *app_name,
                             const char *key, const char *type);
/* Validated settings, on the main event loop. Partial writes are reported through changed. */
int32_t web_app_settings_save(const char *manifest, const char *app_name, const json_t *wanted,
                              bool *changed);
