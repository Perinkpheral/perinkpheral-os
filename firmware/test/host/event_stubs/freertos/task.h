/* SPDX-License-Identifier: MIT */
#pragma once
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
void vTaskDelay(unsigned ticks);
void vTaskPrioritySet(TaskHandle_t task, unsigned priority);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
