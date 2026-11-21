/* SPDX-License-Identifier: MIT */
#pragma once
#include "../../install_stubs/freertos/FreeRTOS.h"
#include "../../install_stubs/freertos/semphr.h"
typedef int BaseType_t;
typedef unsigned TickType_t;
typedef void *TaskHandle_t;
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(milliseconds) (milliseconds)
SemaphoreHandle_t xSemaphoreCreateBinary(void);
BaseType_t xTaskCreate(void (*task)(void *), const char *name, unsigned stack,
                       void *argument, unsigned priority, TaskHandle_t *handle);
void xTaskNotifyGive(TaskHandle_t task);
unsigned ulTaskNotifyTake(int clear, TickType_t timeout);
