// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "vault_host.h"
#include <stdio.h>
#include <stdlib.h>
#define ESP_ERR_INVALID_VERSION 0x10A
#define ESP_ERROR_CHECK(expression) do { esp_err_t result_ = (expression); if (result_ != ESP_OK) { fprintf(stderr, "%s:%d: error %d\n", __FILE__, __LINE__, (int)result_); abort(); } } while (0)
static inline const char *esp_err_to_name(esp_err_t error) { (void)error; return "simulated service error"; }
