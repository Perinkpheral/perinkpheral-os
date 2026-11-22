/* SPDX-License-Identifier: MIT */
#pragma once
#include "FreeRTOS.h"
typedef struct test_queue *QueueHandle_t;
typedef void *QueueSetHandle_t;
QueueHandle_t xQueueCreate(unsigned length, unsigned item_size);
QueueSetHandle_t xQueueCreateSet(unsigned length);
int xQueueAddToSet(QueueHandle_t queue, QueueSetHandle_t set);
int xQueueSend(QueueHandle_t queue, const void *item, unsigned wait);
int xQueueReceive(QueueHandle_t queue, void *item, unsigned wait);
unsigned uxQueueMessagesWaiting(QueueHandle_t queue);
unsigned uxQueueSpacesAvailable(QueueHandle_t queue);
QueueHandle_t xQueueSelectFromSet(QueueSetHandle_t set, unsigned wait);
