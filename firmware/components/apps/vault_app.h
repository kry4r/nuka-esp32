// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "app.h"
#include "vault.h"
typedef enum { VAULT_PAGE_PIN, VAULT_PAGE_ENTRIES, VAULT_PAGE_ENTRY, VAULT_PAGE_GENERATOR,
    VAULT_PAGE_SAVE, VAULT_PAGE_TYPE } vault_page_t;
typedef enum { VUI_DIGIT, VUI_ERASE, VUI_SUBMIT, VUI_OPEN, VUI_PREVIOUS, VUI_NEXT,
    VUI_ENTRY, VUI_LOCK, VUI_MANAGE, VUI_GENERATOR, VUI_LENGTH, VUI_CHARSET,
    VUI_GENERATE, VUI_SAVE_FORM, VUI_FIELD, VUI_CHARACTER, VUI_SAVE,
    VUI_DELETE, VUI_REVEAL, VUI_TYPE, VUI_HOLD, VUI_CANCEL } vault_command_t;
typedef struct {
    vault_page_t page;
    vault_status_t status;
    vault_generator_t generator;
    size_t count, selected;
    uint32_t remaining;
    bool busy, confirming_pin, stale, revealed, generated_visible;
    const char *message;
    char pin_mask[9], name[256], user[256], url[256], note[256], code[9];
    char hidden[65], revealed_password[256], generated[65], form[2][129];
    unsigned field;
} vault_app_model_t;
extern const app_def_t vault_app;
void vault_app_tick(void);
void vault_app_next(void);
void vault_app_primary(void);
void vault_app_command(vault_command_t command, int value);
const vault_app_model_t *vault_app_model(void);
void vault_view_update(bool rebuild);
