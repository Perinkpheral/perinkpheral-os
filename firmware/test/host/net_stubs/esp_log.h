/* SPDX-License-Identifier: MIT */
#pragma once
#include "../persistence_stubs/esp_log.h"
#define ESP_LOG_INFO 3
#define ESP_LOG_WARN 2
#define ESP_LOG_LEVEL(level, tag, ...) do { (void)(level); ESP_LOGW(tag, __VA_ARGS__); } while (0)
