// SPDX-License-Identifier: AGPL-3.0-or-later
#include "i18n.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const char *key, *en, *zh; } string_t;
static const string_t strings[] = {
#include "strings.inc"
};
static bool chinese;

void i18n_set_language(const char *language)
{
    chinese = strcmp(language, "zh-CN") == 0;
}

static int compare_key(const void *key, const void *entry)
{
    return strcmp(key, ((const string_t *)entry)->key);
}

const char *tr(const char *key)
{
    const string_t *entry = bsearch(key, strings, sizeof(strings) / sizeof(strings[0]),
                                    sizeof(strings[0]), compare_key);
    return chinese ? entry->zh : entry->en;
}
