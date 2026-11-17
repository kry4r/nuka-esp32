// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#ifdef ESP_PLATFORM
#include "esp_err.h"
#else
#include "vault_host.h"
#endif

typedef enum { VAULT_TOTP, VAULT_PASSWORD } vault_kind_t;
typedef enum { VAULT_SHA1, VAULT_SHA256, VAULT_SHA512 } vault_algorithm_t;
typedef struct {
    uint32_t id;
    vault_kind_t kind;
    const char *name;
    union {
        struct {
            const uint8_t *secret;
            size_t secret_len;
            uint8_t digits;
            uint32_t period;
            vault_algorithm_t algorithm;
        } totp;
        struct { const char *user, *password, *url, *note; } password;
    } data;
} vault_entry_t;
typedef struct { size_t added, skipped; } vault_import_preview_t;
typedef struct {
    bool configured, hardware_key, unlocked, usb_admin;
    uint32_t failures, cooldown_s, iterations;
    uint16_t unlock_seconds;
    uint8_t wipe_after;
} vault_status_t;
typedef struct {
    uint8_t length;
    bool upper, lower, digits, symbols, exclude_ambiguous;
} vault_generator_t;

// Operations use an internal recursive mutex. PIN/password input stays in internal RAM.
void vault_guard_enter(void);
void vault_guard_leave(void);
esp_err_t vault_init(void);
esp_err_t vault_use_hardware_key(uint8_t slot);
vault_status_t vault_status(void);
esp_err_t vault_set_pin(const char *pin, size_t length);
esp_err_t vault_unlock(const char *pin, size_t length);
void vault_lock(void);
bool vault_unlocked(void);
void vault_activity(void);
esp_err_t vault_tick(void);
esp_err_t vault_set_policy(uint8_t wipe_after, uint16_t unlock_seconds);
esp_err_t vault_management_begin(void);
bool vault_management_active(void);

// Hold the guard while using returned entries, which expire on mutation or lock.
size_t vault_entry_count(void);
const vault_entry_t *vault_entry_at(size_t index);
esp_err_t vault_add(const vault_entry_t *entry);
esp_err_t vault_delete(uint32_t id);
esp_err_t vault_totp(const vault_entry_t *entry, time_t now, char code[9], uint32_t *remaining);
esp_err_t vault_generate(const vault_generator_t *options, char password[65]);

// Export is called only after the device's local confirmation.
esp_err_t vault_export(const uint8_t key[32], const uint8_t salt[16],
                       uint8_t **encrypted, size_t *length);
void vault_free_buffer(uint8_t *buffer, size_t length);
esp_err_t vault_import_begin(const uint8_t key[32], size_t size);
esp_err_t vault_import_chunk(const uint8_t *data, size_t length);
esp_err_t vault_import_finish(vault_import_preview_t *preview);
esp_err_t vault_otpauth_preview(const char *const *uris, size_t count,
                                vault_import_preview_t *preview);
const vault_entry_t *vault_import_entry_at(size_t index);
esp_err_t vault_import_commit(void);
void vault_import_cancel(void);
