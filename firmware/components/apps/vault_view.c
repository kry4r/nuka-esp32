// SPDX-License-Identifier: AGPL-3.0-or-later
#include "vault_app.h"
#include "theme.h"
#include "nav.h"
#include "i18n.h"
#include "usb.h"
#include "nukapack.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static lv_obj_t *root, *body, *message, *name, *code, *password, *mask, *countdown, *length_label;
static lv_obj_t *card_root, *page_root;
static lv_obj_t *form_labels[2], *progress;
static bool detail;
static view_state_t forced_state, rendered_state;
static vault_page_t rendered_page;
static size_t rendered_count;
static bool rendered_configured, rendered_unlocked;
static int64_t hold_start;
static DRAM_ATTR char remaining_text[48], length_text[48];

static lv_obj_t *static_label(lv_obj_t *parent, const char *text, int size)
{
    lv_obj_t *label = ui_label(parent, "", size);
    lv_label_set_text_static(label, text); return label;
}
static void command_event(lv_event_t *event)
{
    uintptr_t packed = (uintptr_t)lv_event_get_user_data(event);
    vault_app_command((vault_command_t)(packed >> 16), packed & 65535);
}
static lv_obj_t *button(lv_obj_t *parent, const char *key, vault_command_t command, unsigned value)
{
    uintptr_t packed = ((uintptr_t)command << 16) | value;
    lv_obj_t *object = ui_button(parent, tr(key), command_event, (void *)packed);
    if (detail && vault_app_model()->page != VAULT_PAGE_PIN) lv_group_add_obj(nav_focus_group(), object);
    return object;
}
static void noop(lv_event_t *event) { (void)event; }
static void hold_event(lv_event_t *event)
{
    lv_event_code_t type = lv_event_get_code(event);
    if (type == LV_EVENT_PRESSED) hold_start = esp_timer_get_time();
    if (type == LV_EVENT_RELEASED || type == LV_EVENT_PRESS_LOST) hold_start = 0;
    if (type == LV_EVENT_PRESSING && hold_start && esp_timer_get_time() - hold_start >= 500000) {
        hold_start = 0; vault_app_command(VUI_HOLD, 0);
    }
}
static void reveal_event(lv_event_t *event)
{ (void)event; vault_app_command(VUI_REVEAL, 0); }
static void slider_event(lv_event_t *event)
{ vault_app_command(VUI_LENGTH, lv_slider_get_value(lv_event_get_target(event))); }
static void check_event(lv_event_t *event)
{ vault_app_command(VUI_CHARSET, (uintptr_t)lv_event_get_user_data(event)); }
static void key_button(lv_obj_t *parent, const char *text, vault_command_t command, unsigned value, int32_t width, int32_t height)
{
    lv_obj_t *key = ui_button(parent, text, command_event, (void *)(((uintptr_t)command << 16) | value));
    lv_obj_set_size(key, width, height); lv_obj_set_style_radius(key, NUKA_RADIUS_BTN, 0);
}
static lv_obj_t *key_grid(lv_obj_t *parent, int32_t width)
{
    lv_obj_t *keys = ui_box(parent, width, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(keys, LV_FLEX_FLOW_ROW_WRAP); lv_obj_set_style_pad_gap(keys, NUKA_SPACE_S, 0);
    lv_obj_set_flex_align(keys, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return keys;
}
// SPEC §6.6.3 pinpad: 3x4 keys of 112x80, erase and submit on the bottom row beside 0.
static void pinpad(lv_obj_t *parent, bool configured)
{
    lv_obj_t *keys = key_grid(parent, 3 * 112 + 2 * NUKA_SPACE_S);
    for (unsigned digit = 1; digit <= 9; ++digit) { char text[2] = {(char)('0' + digit), 0}; key_button(keys, text, VUI_DIGIT, digit, 112, 80); }
    lv_obj_t *erase = ui_icon_button(keys, ICON_BACK, 80, false, command_event, (void *)((uintptr_t)VUI_ERASE << 16));
    lv_obj_set_size(erase, 112, 80); lv_obj_set_style_radius(erase, NUKA_RADIUS_BTN, 0);
    key_button(keys, "0", VUI_DIGIT, 0, 112, 80);
    lv_obj_t *submit = ui_icon_button(keys, ICON_CHECK, 80, true, command_event, (void *)((uintptr_t)VUI_SUBMIT << 16));
    lv_obj_set_size(submit, 112, 80); lv_obj_set_style_radius(submit, NUKA_RADIUS_BTN, 0);
    (void)configured;
}
static void keyboard(lv_obj_t *parent)
{
    lv_obj_t *keys = key_grid(parent, LV_PCT(100));
    const char *characters = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 @._-+";
    for (const char *at = characters; *at; ++at) {
        char text[2] = {*at, 0};
        key_button(keys, text, VUI_CHARACTER, (unsigned char)*at, 54, 56);
    }
    button(parent, "vault.backspace", VUI_ERASE, 0);
}
static void pin_page(const vault_app_model_t *m)
{
    const char *key = m->status.configured ? "vault.pin" : m->confirming_pin ? "vault.confirm_pin" : "vault.setup";
    lv_obj_t *prompt = ui_label(body, tr(key), 28);
    lv_obj_set_style_text_align(prompt, LV_TEXT_ALIGN_CENTER, 0); lv_obj_set_style_text_color(prompt, lv_color_hex(NUKA_COLOR_TEXT_DIM), 0);
    mask = static_label(body, m->pin_mask, 40); lv_obj_set_style_text_align(mask, LV_TEXT_ALIGN_CENTER, 0);
    if (m->status.cooldown_s) { lv_obj_t *wait = ui_label(body, tr("vault.cooldown"), 22); lv_obj_set_style_text_color(wait, lv_color_hex(NUKA_COLOR_WARN), 0); }
    else if (!m->busy) pinpad(body, m->status.configured);
}
static void entry_summary(const vault_app_model_t *m, bool expanded)
{
    if (!m->count) { ui_label(body, tr("vault.empty"), 28); return; }
    name = static_label(body, m->name, 28);
    if (*m->code) {
        code = static_label(body, m->code, expanded ? 56 : 40);
        if (!expanded) lv_obj_set_style_text_font(code, nuka_mono_font(), 0);
        lv_obj_set_style_text_color(code, lv_color_hex(NUKA_COLOR_AVO), 0);
        countdown = static_label(body, remaining_text, 22);
        progress = ui_ring(body, 72, 8);
        if (m->stale) ui_label(body, tr("vault.stale"), 22);
    } else if (expanded) {
        static_label(body, m->user, 22); static_label(body, m->url, 22); static_label(body, m->note, 22);
        password = static_label(body, m->revealed ? m->revealed_password : m->hidden, 28);
        lv_obj_t *reveal = ui_button(body, tr("vault.reveal"), noop, NULL);
        lv_obj_add_event_cb(reveal, reveal_event, LV_EVENT_LONG_PRESSED, NULL);
    }
    if (!expanded) button(body, "vault.open", VUI_ENTRY, 0);
    else if (*m->code) button(body, "vault.type", VUI_TYPE, 0);
    else {
        button(body, "vault.username", VUI_TYPE, 0);
        button(body, "vault.password", VUI_TYPE, 1);
        button(body, "vault.combined", VUI_TYPE, 2);
    }
    if (expanded) button(body, "vault.delete", VUI_DELETE, 0);
    button(body, "vault.previous", VUI_PREVIOUS, 0);
    button(body, "vault.next", VUI_NEXT, 0);
}
static void generator_page(const vault_app_model_t *m)
{
    length_label = static_label(body, length_text, 22);
    lv_obj_t *slider = lv_slider_create(body); lv_obj_set_width(slider, LV_PCT(90));
    lv_slider_set_range(slider, 12, 64); lv_slider_set_value(slider, m->generator.length, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, slider_event, LV_EVENT_VALUE_CHANGED, NULL);
    const char *keys[] = { "vault.upper", "vault.lower", "vault.digits", "vault.symbols", "vault.ambiguous" };
    bool values[] = { m->generator.upper, m->generator.lower, m->generator.digits,
        m->generator.symbols, m->generator.exclude_ambiguous };
    for (unsigned i = 0; i < 5; ++i) {
        lv_obj_t *check = lv_checkbox_create(body); lv_checkbox_set_text_static(check, tr(keys[i]));
        if (values[i]) lv_obj_add_state(check, LV_STATE_CHECKED);
        lv_obj_add_event_cb(check, check_event, LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)i);
        lv_group_add_obj(nav_focus_group(), check);
    }
    button(body, "vault.generate", VUI_GENERATE, 0);
    if (*m->generated) {
        password = static_label(body, m->generated_visible ? m->generated : m->hidden, 22);
        lv_obj_t *reveal = ui_button(body, tr("vault.reveal"), noop, NULL);
        lv_obj_add_event_cb(reveal, reveal_event, LV_EVENT_LONG_PRESSED, NULL);
        button(body, "vault.save", VUI_SAVE_FORM, 0);
        button(body, "vault.type", VUI_TYPE, 0);
    }
}
static void render(void)
{
    const vault_app_model_t *m = vault_app_model();
    lv_obj_clean(root); ui_column(root);
    body = ui_box(root, LV_PCT(100), LV_SIZE_CONTENT); ui_column(body);
    lv_obj_set_width(body, LV_PCT(100)); lv_obj_set_height(body, LV_SIZE_CONTENT);
    message = name = code = password = mask = countdown = progress = length_label = NULL;
    form_labels[0] = form_labels[1] = NULL;
    rendered_page = m->page; rendered_count = m->count;
    rendered_state = forced_state;
    rendered_configured = m->status.configured; rendered_unlocked = m->status.unlocked;
    if (forced_state == VIEW_LOADING || forced_state == VIEW_UNCONFIGURED) {
        ui_label(body, tr(forced_state == VIEW_LOADING ? "state.loading" : "vault.setup"), 28); return;
    }
    if (forced_state == VIEW_OFFLINE) ui_label(body, tr("vault.usb_missing"), 22);
    lv_obj_t *warning = NULL;
    if (!m->status.hardware_key) {
        warning = ui_label(body, tr("vault.degraded"), 22);
        lv_obj_set_style_text_color(warning, lv_color_hex(NUKA_COLOR_DANGER), 0); lv_obj_set_style_text_align(warning, LV_TEXT_ALIGN_CENTER, 0);
    }
    if (!detail) {
        // Locked: one glyph, one word, one action. Unlocked: the current entry is the hero.
        if (m->status.unlocked) entry_summary(m, false);
        else {
            ui_icon(body, ICON_LOCK, 96, NUKA_COLOR_AVO);
            lv_obj_t *state = ui_label(body, tr("vault.locked"), 28);
            lv_obj_set_style_text_align(state, LV_TEXT_ALIGN_CENTER, 0); lv_obj_set_style_text_color(state, lv_color_hex(NUKA_COLOR_TEXT_DIM), 0);
            uintptr_t packed = (uintptr_t)VUI_OPEN << 16;
            lv_obj_t *open = ui_button_primary(body, tr("action.unlock"), command_event, (void *)packed); lv_obj_set_width(open, 240);
        }
        lv_obj_set_flex_align(root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_flex_align(body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        if (warning) lv_obj_move_to_index(warning, -1);
    } else if (m->page == VAULT_PAGE_PIN) pin_page(m);
    else if (m->page == VAULT_PAGE_ENTRIES || m->page == VAULT_PAGE_ENTRY) {
        entry_summary(m, m->page == VAULT_PAGE_ENTRY);
        button(body, "vault.generator", VUI_GENERATOR, 0);
        button(body, "vault.manage", VUI_MANAGE, 0);
        button(body, "vault.lock", VUI_LOCK, 0);
    } else if (m->page == VAULT_PAGE_GENERATOR) generator_page(m);
    else if (m->page == VAULT_PAGE_SAVE) {
        button(body, "vault.name", VUI_FIELD, 0); form_labels[0] = static_label(body, m->form[0], 22);
        button(body, "vault.username", VUI_FIELD, 1); form_labels[1] = static_label(body, m->form[1], 22);
        keyboard(body); button(body, "vault.save", VUI_SAVE, 0);
    } else {
        static_label(body, m->name, 28);
        lv_obj_t *hold = ui_button(body, tr("vault.hold"), noop, NULL);
        lv_obj_add_event_cb(hold, hold_event, LV_EVENT_ALL, NULL);
        lv_group_add_obj(nav_focus_group(), hold);
    }
    if (detail) button(body, "vault.cancel", VUI_CANCEL, 0);
    message = ui_label(body, "", 22);
}
void vault_view_update(bool rebuild)
{
    const vault_app_model_t *m = vault_app_model();
    bool current_detail = nav_in_detail();
    lv_obj_t *current_root = current_detail ? page_root : card_root;
    if (root != current_root) { root = current_root; detail = current_detail; rebuild = true; }
    if (!root) return;
    if (rebuild || rendered_state != forced_state || rendered_page != m->page || rendered_count != m->count ||
        rendered_configured != m->status.configured || rendered_unlocked != m->status.unlocked) render();
    if (message) lv_label_set_text_static(message, m->message ? tr(m->message) : "");
    if (mask) lv_label_set_text_static(mask, m->pin_mask);
    if (name) lv_label_set_text_static(name, m->name);
    if (code) lv_label_set_text_static(code, m->code);
    if (password) lv_label_set_text_static(password, m->page == VAULT_PAGE_GENERATOR ?
        (m->generated_visible ? m->generated : m->hidden) : (m->revealed ? m->revealed_password : m->hidden));
    if (countdown) {
        snprintf(remaining_text, sizeof(remaining_text), "%lu s", (unsigned long)m->remaining);
        lv_label_set_text_static(countdown, remaining_text);
        // Each entry carries its own period, so the ring represents its remaining fraction.
        vault_guard_enter();
        if (vault_unlocked() && vault_entry_count()) {
            const vault_entry_t *entry = vault_entry_at(m->selected);
            if (entry->kind == VAULT_TOTP) ui_ring_set(progress, 100.0f * m->remaining / entry->data.totp.period, m->remaining <= 5 ? NUKA_COLOR_WARN : NUKA_COLOR_AVO);
        }
        vault_guard_leave();
    }
    if (length_label) {
        snprintf(length_text, sizeof(length_text), "%s: %u", tr("vault.length"), m->generator.length);
        lv_label_set_text_static(length_label, length_text);
    }
    for (unsigned i = 0; i < 2; ++i) if (form_labels[i]) lv_label_set_text_static(form_labels[i], m->form[i]);
}
// nav cleans its card and page without notifying the view, so drop cached objects when LVGL deletes them.
static void forget_root(lv_event_t *event)
{
    lv_obj_t *object = lv_event_get_target(event);
    if (object == card_root) card_root = NULL;
    if (object == page_root) page_root = NULL;
    if (object != root) return;
    root = body = message = name = code = password = mask = countdown = progress = length_label = NULL;
    form_labels[0] = form_labels[1] = NULL;
}
static void create(lv_obj_t *card)
{
    card_root = root = ui_box(card, LV_PCT(100), LV_PCT(100));
    lv_obj_add_event_cb(root, forget_root, LV_EVENT_DELETE, NULL);
    detail = false; vault_app_tick(); vault_view_update(true);
}
static void apply(const void *vm, view_state_t state)
{ (void)vm; forced_state = state; vault_app_tick(); }
static void open_detail(lv_obj_t *page)
{
    page_root = root = lv_obj_create(page);
    lv_obj_add_event_cb(root, forget_root, LV_EVENT_DELETE, NULL);
    lv_obj_set_size(root, LV_PCT(100), LV_SIZE_CONTENT);
    detail = true; vault_app_tick(); vault_view_update(true);
}
const app_view_t vault_view = { .create = create, .apply = apply, .open_detail = open_detail };
