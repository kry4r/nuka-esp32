// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "FreeRTOS.h"
#include <stddef.h>
QueueHandle_t xQueueCreateStatic(unsigned count, size_t size, uint8_t *bytes, StaticQueue_t *storage);
int xQueueReceive(QueueHandle_t queue, void *output, TickType_t wait);
int xQueueSend(QueueHandle_t queue, const void *input, TickType_t wait);
