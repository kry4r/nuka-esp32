// SPDX-License-Identifier: AGPL-3.0-or-later
#include "vault_app.h"
#include "nav.h"
#include "overlay.h"
#include "i18n.h"
#include "nuka_time.h"
#include "usb.h"
#include "power.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mbedtls/platform_util.h"
#include <stdio.h>
#include <string.h>

extern const app_view_t vault_view;
static DRAM_ATTR vault_app_model_t model;
static DRAM_ATTR char pin[9], first_pin[9];
static DRAM_ATTR struct { char pin[9]; bool setup; } job;
static DRAM_ATTR StackType_t worker_stack[4096];
static DRAM_ATTR StaticTask_t worker_task;
static DRAM_ATTR StaticQueue_t jobs_storage, results_storage;
static DRAM_ATTR uint8_t jobs_bytes[1], results_bytes[sizeof(esp_err_t)];
static QueueHandle_t jobs, results;
static int64_t reveal_until, away_since;
static size_t pin_length, selected;
static int typing;
static bool generated_source;

static void clear_display(void)
{
    mbedtls_platform_zeroize(model.name, sizeof(model.name));
    mbedtls_platform_zeroize(model.user, sizeof(model.user));
    mbedtls_platform_zeroize(model.url, sizeof(model.url));
    mbedtls_platform_zeroize(model.note, sizeof(model.note));
    mbedtls_platform_zeroize(model.code, sizeof(model.code));
    mbedtls_platform_zeroize(model.revealed_password, sizeof(model.revealed_password));
    model.revealed = false;
}
static void clear_input(void)
{
    mbedtls_platform_zeroize(pin, sizeof(pin)); pin_length = 0;
    mbedtls_platform_zeroize(model.pin_mask, sizeof(model.pin_mask));
}
static void clear_local(void)
{
    clear_display(); clear_input();
    mbedtls_platform_zeroize(first_pin, sizeof(first_pin));
    mbedtls_platform_zeroize(model.generated, sizeof(model.generated));
    mbedtls_platform_zeroize(model.form, sizeof(model.form));
    reveal_until = 0; model.confirming_pin = model.generated_visible = false;
}
static void pin_worker(void *unused)
{
    (void)unused;
    for (;;) {
        uint8_t work; xQueueReceive(jobs, &work, portMAX_DELAY);
        esp_err_t error = job.setup ? vault_set_pin(job.pin, strlen(job.pin)) : vault_unlock(job.pin, strlen(job.pin));
        if (error == ESP_OK && !power_screen_on()) { vault_lock(); error = ESP_ERR_INVALID_STATE; }
        mbedtls_platform_zeroize(job.pin, sizeof(job.pin));
        xQueueSend(results, &error, portMAX_DELAY);
    }
}
static void start_worker(void)
{
    jobs = xQueueCreateStatic(1, 1, jobs_bytes, &jobs_storage);
    results = xQueueCreateStatic(1, sizeof(esp_err_t), results_bytes, &results_storage);
    xTaskCreateStatic(pin_worker, "vault_pin", 4096, NULL, 3, worker_stack, &worker_task);
    model.generator = (vault_generator_t){ .length = 20, .upper = true, .lower = true,
        .digits = true, .symbols = true, .exclude_ambiguous = true };
    memset(model.hidden, '*', 12); model.hidden[12] = 0;
}
static void snapshot(void)
{
    model.status = vault_status();
    model.stale = nuka_time_stale();
    vault_guard_enter();
    model.count = vault_entry_count();
    if (model.count && selected >= model.count) selected = model.count - 1;
    model.selected = selected;
    if (!model.status.unlocked) {
        clear_display(); model.page = VAULT_PAGE_PIN;
    } else if (model.count) {
        const vault_entry_t *entry = vault_entry_at(selected);
        snprintf(model.name, sizeof(model.name), "%s", entry->name);
        if (entry->kind == VAULT_TOTP) {
            time_t now = time(NULL);
            if (now > 0) {
                esp_err_t error = vault_totp(entry, now, model.code, &model.remaining);
                if (error != ESP_OK) model.message = "vault.failed";
            } else { model.code[0] = 0; model.message = "vault.clock"; }
            model.user[0] = model.url[0] = model.note[0] = 0;
        } else {
            model.code[0] = 0;
            snprintf(model.user, sizeof(model.user), "%s", entry->data.password.user);
            snprintf(model.url, sizeof(model.url), "%s", entry->data.password.url);
            snprintf(model.note, sizeof(model.note), "%s", entry->data.password.note);
        }
    } else clear_display();
    vault_guard_leave();
}
const vault_app_model_t *vault_app_model(void) { return &model; }
static void result_message(esp_err_t error)
{
    model.message = error == ESP_OK ? "vault.saved" : "vault.failed";
}
static void submit_pin(void)
{
    if (model.busy || pin_length < 6) return;
    if (!model.status.configured && !model.confirming_pin) {
        memcpy(first_pin, pin, sizeof(pin)); clear_input(); model.confirming_pin = true; return;
    }
    if (!model.status.configured && strcmp(first_pin, pin)) {
        model.message = "vault.pin_mismatch"; clear_input();
        mbedtls_platform_zeroize(first_pin, sizeof(first_pin)); model.confirming_pin = false; return;
    }
    memcpy(job.pin, pin, sizeof(pin)); job.setup = !model.status.configured;
    clear_input(); mbedtls_platform_zeroize(first_pin, sizeof(first_pin));
    model.confirming_pin = false; model.busy = true; model.message = "vault.busy";
    uint8_t work = 1; xQueueSend(jobs, &work, portMAX_DELAY);
}
static void type_selected(void)
{
    if (!usb_attached()) { model.message = "vault.usb_missing"; return; }
    if (generated_source) { usb_type_text(model.generated); return; }
    vault_guard_enter();
    if (vault_unlocked() && vault_entry_count()) {
        const vault_entry_t *entry = vault_entry_at(selected);
        if (entry->kind == VAULT_TOTP) {
            char code[9]; uint32_t remaining;
            esp_err_t error = vault_totp(entry, time(NULL), code, &remaining);
            if (error == ESP_OK) usb_type_text(code);
            else model.message = "vault.failed";
            mbedtls_platform_zeroize(code, sizeof(code));
        } else if (typing < 2) usb_type_text(typing ? entry->data.password.password : entry->data.password.user);
        else {
            size_t length = strlen(entry->data.password.user) + strlen(entry->data.password.password) + 3;
            char *text = heap_caps_malloc(length, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (!text) model.message = "vault.failed";
            else {
                snprintf(text, length, "%s\t%s\n", entry->data.password.user, entry->data.password.password);
                usb_type_text(text); vault_free_buffer((uint8_t *)text, length);
            }
        }
    }
    vault_guard_leave();
}
void vault_app_command(vault_command_t command, int value)
{
    if (model.busy) return;
    model.message = NULL;
    vault_activity();
    switch (command) {
    case VUI_DIGIT:
        if (pin_length < 8) { pin[pin_length] = '0' + value; model.pin_mask[pin_length++] = '*'; }
        break;
    case VUI_ERASE:
        if (model.page == VAULT_PAGE_SAVE) {
            size_t length = strlen(model.form[model.field]);
            if (length) model.form[model.field][length - 1] = 0;
        } else if (pin_length) { pin[--pin_length] = 0; model.pin_mask[pin_length] = 0; }
        break;
    case VUI_SUBMIT: submit_pin(); break;
    case VUI_OPEN: model.page = model.status.unlocked ? VAULT_PAGE_ENTRIES : VAULT_PAGE_PIN; nav_detail(); break;
    case VUI_PREVIOUS: if (model.count) selected = (selected + model.count - 1) % model.count; break;
    case VUI_NEXT: if (model.count) selected = (selected + 1) % model.count; break;
    case VUI_ENTRY: model.page = VAULT_PAGE_ENTRY; if (!nav_in_detail()) nav_detail(); break;
    case VUI_LOCK: vault_lock(); clear_local(); model.page = VAULT_PAGE_PIN; break;
    case VUI_MANAGE:
        model.message = vault_management_begin() == ESP_OK ? "vault.manage" : "vault.failed"; break;
    case VUI_GENERATOR: model.page = VAULT_PAGE_GENERATOR; break;
    case VUI_LENGTH: model.generator.length = value; break;
    case VUI_CHARSET:
        if (value == 0) model.generator.upper = !model.generator.upper;
        else if (value == 1) model.generator.lower = !model.generator.lower;
        else if (value == 2) model.generator.digits = !model.generator.digits;
        else if (value == 3) model.generator.symbols = !model.generator.symbols;
        else model.generator.exclude_ambiguous = !model.generator.exclude_ambiguous;
        break;
    case VUI_GENERATE:
        model.message = vault_generate(&model.generator, model.generated) == ESP_OK ? NULL : "vault.failed";
        model.generated_visible = false; break;
    case VUI_SAVE_FORM:
        mbedtls_platform_zeroize(model.form, sizeof(model.form)); model.field = 0; model.page = VAULT_PAGE_SAVE; break;
    case VUI_FIELD: model.field = value; break;
    case VUI_CHARACTER: {
        size_t length = strlen(model.form[model.field]);
        if (length < sizeof(model.form[0]) - 1) model.form[model.field][length] = value;
        break;
    }
    case VUI_SAVE: {
        vault_entry_t entry = { .kind = VAULT_PASSWORD, .name = model.form[0],
            .data.password = { .user = model.form[1], .password = model.generated, .url = "", .note = "" } };
        if (!*model.form[0] || !*model.generated) { model.message = "vault.failed"; break; }
        esp_err_t error = vault_add(&entry); result_message(error);
        if (error == ESP_OK) {
            mbedtls_platform_zeroize(model.generated, sizeof(model.generated));
            mbedtls_platform_zeroize(model.form, sizeof(model.form)); model.page = VAULT_PAGE_ENTRIES;
        }
        break;
    }
    case VUI_DELETE:
        vault_guard_enter();
        if (vault_entry_count()) result_message(vault_delete(vault_entry_at(selected)->id));
        vault_guard_leave(); model.page = VAULT_PAGE_ENTRIES; break;
    case VUI_REVEAL:
        if (model.page == VAULT_PAGE_GENERATOR) {
            model.generated_visible = true; reveal_until = esp_timer_get_time() + 5000000;
        } else {
            vault_guard_enter();
            if (vault_unlocked() && vault_entry_count()) {
                const vault_entry_t *entry = vault_entry_at(selected);
                if (entry->kind == VAULT_PASSWORD) {
                    snprintf(model.revealed_password, sizeof(model.revealed_password), "%s", entry->data.password.password);
                    model.revealed = true; reveal_until = esp_timer_get_time() + 5000000;
                }
            }
            vault_guard_leave();
        }
        break;
    case VUI_TYPE:
        typing = value; generated_source = model.page == VAULT_PAGE_GENERATOR;
        model.page = VAULT_PAGE_TYPE; break;
    case VUI_HOLD: type_selected(); model.page = generated_source ? VAULT_PAGE_GENERATOR : VAULT_PAGE_ENTRY; break;
    case VUI_CANCEL:
        clear_input(); model.confirming_pin = false;
        mbedtls_platform_zeroize(first_pin, sizeof(first_pin));
        if (model.page == VAULT_PAGE_PIN) nav_back();
        else if (model.page == VAULT_PAGE_SAVE) {
            mbedtls_platform_zeroize(model.form, sizeof(model.form)); model.page = VAULT_PAGE_GENERATOR;
        } else if (model.page == VAULT_PAGE_TYPE) model.page = generated_source ? VAULT_PAGE_GENERATOR : VAULT_PAGE_ENTRY;
        else model.page = VAULT_PAGE_ENTRIES;
        break;
    }
    if (!model.busy) snapshot();
    vault_view_update(command != VUI_DIGIT && command != VUI_ERASE && command != VUI_CHARACTER &&
                      command != VUI_LENGTH && command != VUI_CHARSET);
}
void vault_app_next(void) { vault_app_command(VUI_NEXT, 0); }
void vault_app_primary(void)
{
    if (model.status.unlocked) {
        if (model.page == VAULT_PAGE_TYPE) vault_app_command(VUI_HOLD, 0);
        else if (model.page == VAULT_PAGE_ENTRY) vault_app_command(VUI_TYPE, 1);
        else vault_app_command(VUI_ENTRY, 0);
    }
    else vault_app_command(VUI_OPEN, 0);
}
void vault_app_tick(void)
{
    if (!jobs) start_worker();
    bool visible = !strcmp(nav_current()->id, "vault");
    esp_err_t result; bool changed = false;
    if (model.busy) {
        if (xQueueReceive(results, &result, 0) != pdTRUE) {
            if (visible) vault_view_update(false);
            return;
        }
        model.busy = false; model.message = result == ESP_OK ? NULL :
            (result == ESP_ERR_TIMEOUT ? "vault.cooldown" : "vault.wrong_pin");
        if (result == ESP_OK) model.page = VAULT_PAGE_ENTRIES;
        changed = true;
    }
    int64_t now = esp_timer_get_time();
    if (visible) away_since = 0;
    else if (!away_since) away_since = now;
    if (!visible && now - away_since >= 30000000 && vault_unlocked()) { vault_lock(); clear_local(); }
    if (reveal_until && now >= reveal_until) {
        mbedtls_platform_zeroize(model.revealed_password, sizeof(model.revealed_password));
        model.revealed = model.generated_visible = false; reveal_until = 0; changed = true;
    }
    if (!vault_unlocked()) {
        bool had_secret = model.status.unlocked;
        if (had_secret) { clear_local(); changed = true; }
    }
    if (visible) { snapshot(); vault_view_update(changed); }
}
static void action(app_action_t action)
{
    if (action == ACT_BACK) {
        away_since = esp_timer_get_time(); clear_input();
        mbedtls_platform_zeroize(first_pin, sizeof(first_pin)); model.confirming_pin = false;
        mbedtls_platform_zeroize(model.revealed_password, sizeof(model.revealed_password)); model.revealed = false;
    } else if (action == ACT_PRIMARY) vault_app_primary();
    else if (action == ACT_SECONDARY) vault_app_command(VUI_LOCK, 0);
}
const app_def_t vault_app = { .id = "vault", .topic = VM_NONE, .view = &vault_view, .on_action = action };
