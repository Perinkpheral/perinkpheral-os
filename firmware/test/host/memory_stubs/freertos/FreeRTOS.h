/* SPDX-License-Identifier: MIT */
#pragma once
#include "../../install_stubs/freertos/FreeRTOS.h"
#ifdef MEMORY_TEST_LOG
void memory_test_enter(portMUX_TYPE *lock);
void memory_test_exit(portMUX_TYPE *lock);
#undef portENTER_CRITICAL
#undef portEXIT_CRITICAL
#define portENTER_CRITICAL(lock) memory_test_enter(lock)
#define portEXIT_CRITICAL(lock) memory_test_exit(lock)
#endif
