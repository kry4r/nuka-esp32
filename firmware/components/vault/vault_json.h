// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "vault.h"
typedef struct vault_json_document vault_json_document_t;
esp_err_t vault_json_open(const char *json, size_t length, vault_json_document_t **document);
void vault_json_close(vault_json_document_t *document);
bool vault_json_object(const vault_json_document_t *document, int token);
bool vault_json_fields(const vault_json_document_t *document, int object, const char *allowed);
int vault_json_field(const vault_json_document_t *document, int object, const char *name);
int vault_json_array_at(const vault_json_document_t *document, int array, size_t index);
int vault_json_array_size(const vault_json_document_t *document, int array);
bool vault_json_u32(const vault_json_document_t *document, int token, uint32_t *value);
bool vault_json_bool(const vault_json_document_t *document, int token, bool *value);
// Returned strings use internal RAM; release with vault_free_buffer(text, strlen(text)+1).
char *vault_json_string(const vault_json_document_t *document, int token);
char *vault_json_raw(const vault_json_document_t *document, int token);
