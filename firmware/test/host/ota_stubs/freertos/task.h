// SPDX-License-Identifier: MIT
#pragma once
#include "FreeRTOS.h"
#include <stddef.h>

BaseType_t xTaskCreate(void (*task)(void *), const char *name, unsigned stack,
                       void *argument, unsigned priority, void *handle);
void vTaskDelay(unsigned ticks);
void vTaskDelete(void *handle) __attribute__((noreturn));
