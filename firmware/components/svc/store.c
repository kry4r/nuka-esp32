// SPDX-License-Identifier: AGPL-3.0-or-later
#include "store.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_random.h"
#include "esp_system.h"
nuka_settings_t nuka_settings;
_Static_assert(offsetof(nuka_settings_t,home_face)==256,"Persisted settings prefix must keep its existing layout");
esp_err_t store_write_string(const char *ns, const char *key, const char *value)
{
    nvs_handle_t h; esp_err_t err = nvs_open(ns, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, key, value);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h); return err;
}
esp_err_t store_read_string(const char *ns, const char *key, char *out, size_t size)
{
    nvs_handle_t h; esp_err_t err = nvs_open(ns, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    err = nvs_get_str(h, key, out, &size); nvs_close(h); return err;
}
esp_err_t store_write_u32(const char *ns, const char *key, uint32_t value)
{
    nvs_handle_t h; esp_err_t err = nvs_open(ns, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u32(h, key, value);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h); return err;
}
esp_err_t store_read_u32(const char *ns, const char *key, uint32_t *out)
{
    nvs_handle_t h; esp_err_t err = nvs_open(ns, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    err = nvs_get_u32(h, key, out); nvs_close(h); return err;
}
esp_err_t store_read_json(const char *ns, const char *key, cJSON **out)
{
    nvs_handle_t h; esp_err_t err = nvs_open(ns, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    size_t len; err = nvs_get_str(h, key, NULL, &len);
    if (err != ESP_OK) { nvs_close(h); return err; }
    char *text = malloc(len);
    err = nvs_get_str(h, key, text, &len); nvs_close(h);
    *out = err == ESP_OK ? cJSON_Parse(text) : NULL;
    free(text);
    return err == ESP_OK && !*out ? ESP_ERR_INVALID_ARG : err;
}
esp_err_t store_write_json(const char *ns, const char *key, const cJSON *data)
{
    char *text = cJSON_PrintUnformatted(data);
    esp_err_t err = store_write_string(ns, key, text);
    free(text); return err;
}
esp_err_t store_save_settings(void)
{
    nvs_handle_t h;
    esp_err_t err=nvs_open("sys",NVS_READWRITE,&h);if(err!=ESP_OK)return err;
    // Keep the existing settings blob readable when an application OTA rolls back.
    nuka_settings_t stored=nuka_settings;stored.local_overrides&=63;
    if(err==ESP_OK)err=nvs_set_blob(h, "settings", &stored, offsetof(nuka_settings_t,home_face));
    if(err==ESP_OK)err=nvs_set_str(h,"home_face",nuka_settings.home_face);
    if(err==ESP_OK)err=nvs_set_u8(h,"volume",nuka_settings.volume);
    if(err==ESP_OK)err=nvs_set_u8(h,"assistant",nuka_settings.assistant);
    if(err==ESP_OK)err=nvs_set_u16(h,"overrides_v2",nuka_settings.local_overrides&~63U);
    if(err==ESP_OK)err=nvs_set_str(h, "lang", nuka_settings.lang);
    if(err==ESP_OK)err=nvs_set_u8(h, "bright", nuka_settings.brightness);
    if(err==ESP_OK)err=nvs_set_u32(h, "t_dim", nuka_settings.t_dim);
    if(err==ESP_OK)err=nvs_set_u32(h, "t_off", nuka_settings.t_off);
    if(err==ESP_OK)err=nvs_set_u8(h, "raise", nuka_settings.raise);
    if(err==ESP_OK)err=nvs_set_u8(h, "flip_act", nuka_settings.flip_action);
    if(err==ESP_OK)err=nvs_set_u8(h, "chg_mode", nuka_settings.charge_mode);
    if(err==ESP_OK)err=nvs_set_u16(h, "chg_ma", (uint16_t[]){100,200,300,400,500}[nuka_settings.charge_ma_index]);
    if(err==ESP_OK)err=nvs_set_str(h, "cards", nuka_settings.cards);
    if(err==ESP_OK)err=nvs_set_u32(h, "asset_slot", nuka_settings.asset_slot);
    if(err==ESP_OK)err=nvs_set_u32(h, "boot_cnt", nuka_settings.boot_count);
    if(err==ESP_OK)err=nvs_commit(h);
    nvs_close(h); return err;
}
esp_err_t store_init(void)
{
    esp_err_t err = nvs_flash_init();
    ESP_ERROR_CHECK(err);
    nuka_settings = (nuka_settings_t){
        .lang = "zh-CN", .brightness = 60, .t_dim = 20, .t_off = 10,
        .raise = true, .clock_style = true, .charge_ma_index = 2,
        .key3_action = "talk", .key3_double_card = "buddy",
        .cards = "clock,hosts,servers,ai,buddy,pomodoro,media,weather,vault,settings",
        .home_face="clock",.volume=70,.assistant=false
    };
    nvs_handle_t h; ESP_ERROR_CHECK(nvs_open("sys", NVS_READWRITE, &h));
    size_t size = offsetof(nuka_settings_t,home_face);
    err = nvs_get_blob(h, "settings", &nuka_settings, &size);
    size_t face_size=sizeof(nuka_settings.home_face);
    esp_err_t face_error=nvs_get_str(h,"home_face",nuka_settings.home_face,&face_size);
    uint8_t assistant=0;
    esp_err_t assistant_error=nvs_get_u8(h,"assistant",&assistant);
    esp_err_t volume_error=nvs_get_u8(h,"volume",&nuka_settings.volume);
    uint16_t extra_overrides=0;
    esp_err_t overrides_error=nvs_get_u16(h,"overrides_v2",&extra_overrides);
    uint32_t slot = 0;
    esp_err_t slot_err = nvs_get_u32(h, "asset_slot", &slot);
    nvs_close(h);
    if (err != ESP_ERR_NVS_NOT_FOUND && err != ESP_OK) return err;
    if(face_error!=ESP_OK&&face_error!=ESP_ERR_NVS_NOT_FOUND)return face_error;
    if(assistant_error!=ESP_OK&&assistant_error!=ESP_ERR_NVS_NOT_FOUND)return assistant_error;
    if(volume_error!=ESP_OK&&volume_error!=ESP_ERR_NVS_NOT_FOUND)return volume_error;
    if(overrides_error!=ESP_OK&&overrides_error!=ESP_ERR_NVS_NOT_FOUND)return overrides_error;
    if(assistant>1||nuka_settings.volume>100||(extra_overrides&~448U))return ESP_ERR_INVALID_ARG;
    if(strcmp(nuka_settings.home_face,"clock")&&strcmp(nuka_settings.home_face,"assistant"))return ESP_ERR_INVALID_ARG;
    nuka_settings.assistant=assistant;
    if (size != offsetof(nuka_settings_t,home_face) || nuka_settings.brightness > 100 ||
        nuka_settings.t_dim == 0 || nuka_settings.t_dim > 86400 ||
        nuka_settings.t_off == 0 || nuka_settings.t_off > 86400 || nuka_settings.charge_ma_index > 4 ||
        nuka_settings.charge_mode > 1 || nuka_settings.flip_action > 2 ||
        nuka_settings.key3_macro > 11 || nuka_settings.local_overrides > 63 || slot > 1 ||
        !memchr(nuka_settings.lang, 0, sizeof(nuka_settings.lang)) ||
        !memchr(nuka_settings.key3_action, 0, sizeof(nuka_settings.key3_action)) ||
        !memchr(nuka_settings.key3_double_card, 0, sizeof(nuka_settings.key3_double_card)) ||
        !memchr(nuka_settings.cards, 0, sizeof(nuka_settings.cards)))
        return ESP_ERR_INVALID_ARG;
    if (strcmp(nuka_settings.lang, "zh-CN") && strcmp(nuka_settings.lang, "en")) return ESP_ERR_INVALID_ARG;
    const char *actions[] = {"talk","dnd","always_on","pomodoro","macro","none"}; bool action_valid = false;
    for (unsigned i=0;i<6;++i) action_valid |= !strcmp(actions[i],nuka_settings.key3_action);
    if (!action_valid) return ESP_ERR_INVALID_ARG;
    const char *cards[] = {"clock","hosts","servers","ai","buddy","pet","pomodoro","media","weather","vault","settings"};
    bool target_valid=false;for(unsigned i=0;i<11;++i)target_valid|=!strcmp(cards[i],nuka_settings.key3_double_card);
    if(!target_valid)return ESP_ERR_INVALID_ARG;
    unsigned seen=0;const char *card=nuka_settings.cards;
    char visible_cards[sizeof(nuka_settings.cards)]={0};
    do { const char *end=strchr(card,',');size_t length=end?(size_t)(end-card):strlen(card);unsigned index;
        for(index=0;index<11;++index)if(strlen(cards[index])==length&&!memcmp(cards[index],card,length))break;
        if(index==11||(seen&(1U<<index)))return ESP_ERR_INVALID_ARG;
        if(index!=5){size_t used=strlen(visible_cards);if(used)visible_cards[used++]=',';memcpy(visible_cards+used,card,length);}
        seen|=1U<<index;card=end?end+1:NULL;
    }while(card);
    // The standalone pet card became the clock app's second face.
    strcpy(nuka_settings.cards,visible_cards[0]?visible_cards:"clock");
    if(!strcmp(nuka_settings.key3_double_card,"pet"))strcpy(nuka_settings.key3_double_card,"clock");
    if (slot_err != ESP_OK && slot_err != ESP_ERR_NVS_NOT_FOUND) return slot_err;
    nuka_settings.local_overrides|=extra_overrides;
    nuka_settings.asset_slot = slot;
    ++nuka_settings.boot_count;
    ESP_ERROR_CHECK(store_save_settings());
    char id[33];
    if (store_read_string("prov", "dev_id", id, sizeof(id)) == ESP_ERR_NVS_NOT_FOUND) {
        snprintf(id, sizeof(id), "nuka_%08lx", (unsigned long)esp_random());
        ESP_ERROR_CHECK(store_write_string("prov", "dev_id", id));
    }
    return ESP_OK;
}
esp_err_t store_factory_reset(bool erase_vault)
{
    esp_err_t err=nvs_flash_erase();
    if(err==ESP_OK&&erase_vault)err=nvs_flash_erase_partition("vault");
    return err;
}
