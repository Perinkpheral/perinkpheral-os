/* SPDX-License-Identifier: MIT */
#pragma once
#include <assert.h>
#include <stdint.h>
typedef unsigned UBaseType_t;
typedef uint32_t TickType_t;
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) (ms)
#define configASSERT(condition) assert(condition)
