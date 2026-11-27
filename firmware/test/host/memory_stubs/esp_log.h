/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdio.h>
#define ESP_LOGE(tag, ...) do { (void)(tag); if (0) printf(__VA_ARGS__); } while (0)
#ifdef MEMORY_TEST_LOG
void memory_test_log(const char *format, ...) __attribute__((format(printf, 1, 2)));
#define ESP_LOGW(tag, ...) do { (void)(tag); memory_test_log(__VA_ARGS__); } while (0)
#else
#define ESP_LOGW(tag, ...) ESP_LOGE(tag, __VA_ARGS__)
#endif
