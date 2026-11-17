// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "FreeRTOS.h"
SDL_Thread *xTaskCreateStatic(void (*function)(void *), const char *name, unsigned stack,
                            void *argument, unsigned priority, StackType_t *memory, StaticTask_t *storage);
