// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <stdarg.h>
#include <stdio.h>
static inline void sim_log(const char *tag, const char *format, ...)
{
    fprintf(stderr, "%s: ", tag); va_list arguments; va_start(arguments, format);
    vfprintf(stderr, format, arguments); va_end(arguments); fputc('\n', stderr);
}
#define ESP_LOGE sim_log
#define ESP_LOGW sim_log
#define ESP_LOGI sim_log
