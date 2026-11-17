// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <time.h>
#include <string.h>
#if defined(_MSC_VER)
#define __attribute__(attributes)
static inline struct tm *localtime_r(const time_t *value, struct tm *output)
{ return localtime_s(output, value) ? NULL : output; }
static inline struct tm *gmtime_r(const time_t *value, struct tm *output)
{ return gmtime_s(output, value) ? NULL : output; }
static inline char *strtok_r(char *text, const char *separators, char **context)
{ return strtok_s(text, separators, context); }
#endif
