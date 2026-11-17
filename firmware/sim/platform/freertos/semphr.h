// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "FreeRTOS.h"
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return SDL_CreateMutex(); }
static inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutexStatic(StaticSemaphore_t *storage)
{ (void)storage; return SDL_CreateMutex(); }
static inline int xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t wait)
{ (void)wait; return SDL_LockMutex(mutex) == 0; }
static inline int xSemaphoreGive(SemaphoreHandle_t mutex) { return SDL_UnlockMutex(mutex) == 0; }
#define xSemaphoreTakeRecursive xSemaphoreTake
#define xSemaphoreGiveRecursive xSemaphoreGive
