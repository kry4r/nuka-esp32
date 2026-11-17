// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
typedef struct { size_t size; unsigned subtype; } esp_partition_t;
typedef int esp_partition_mmap_handle_t;
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_DATA_NVS 2
#define ESP_PARTITION_MMAP_DATA 0
const esp_partition_t *esp_partition_find_first(unsigned type, unsigned subtype, const char *label);
esp_err_t esp_partition_read(const esp_partition_t *partition, size_t offset, void *output, size_t size);
esp_err_t esp_partition_mmap(const esp_partition_t *partition, size_t offset, size_t size, unsigned mode, const void **output, esp_partition_mmap_handle_t *handle);
void esp_partition_munmap(esp_partition_mmap_handle_t handle);
