/* SPDX-License-Identifier: MIT */
#pragma once
#include "../../install_stubs/freertos/FreeRTOS.h"
#define pdPASS 1
#define pdMS_TO_TICKS(milliseconds) (milliseconds)
void vTaskDelete(void *task);
void vTaskDelay(unsigned ticks);
int xTaskCreate(void (*task)(void *), const char *name, unsigned stack, void *argument, unsigned priority, void *handle);
