// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <stdint.h>
#include "SDL.h"
typedef uint32_t StackType_t;
typedef int StaticTask_t;
typedef int StaticQueue_t;
typedef int StaticSemaphore_t;
typedef uint32_t TickType_t;
#define portMAX_DELAY UINT32_MAX
#define pdTRUE 1
#define pdMS_TO_TICKS(ms) (ms)
typedef struct sim_queue *QueueHandle_t;
typedef SDL_mutex *SemaphoreHandle_t;
