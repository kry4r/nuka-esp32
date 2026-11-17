// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <stdlib.h>
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_SPIRAM 4
static inline void *heap_caps_malloc(size_t size, unsigned capabilities) { (void)capabilities; return malloc(size); }
static inline void *heap_caps_calloc(size_t count, size_t size, unsigned capabilities) { (void)capabilities; return calloc(count, size); }
static inline void heap_caps_free(void *memory) { free(memory); }
static inline size_t heap_caps_get_free_size(unsigned capabilities) { (void)capabilities; return 0; }
