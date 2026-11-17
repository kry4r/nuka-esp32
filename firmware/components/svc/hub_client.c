// SPDX-License-Identifier: AGPL-3.0-or-later
#include "hub_client.h"
#include "proto.h"
#include "store.h"
#include "net.h"
#include "power.h"
#include "battery.h"
#include "macro.h"
#include "nuka_time.h"
#include "nuka_events.h"
#include "esp_websocket_client.h"
#include "esp_crt_bundle.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_task_wdt.h"
#include "mdns.h"
#include "nvs.h"
#include "ota.h"
#include "nukapack.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>
typedef struct { int kind, code; char *text;unsigned generation; } ws_message_t;
enum { WS_OPEN, WS_CLOSED, WS_FRAME, WS_SEND };
static QueueHandle_t queue;
static esp_websocket_client_handle_t client;
static bool connected, stopped;
static cJSON *features;
static char current_url[512], current_card[16] = "clock";
static unsigned request_id;
static unsigned generation;
static atomic_bool reconfigure;
static atomic_bool state_requested;
static int received;
static char *frame_buffer;
static char *cfg_token;
static bool subscribed[] = {true,true,true,true,true,true,true,true,true,true};
static void event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    esp_websocket_event_data_t *e = data;
    ws_message_t message = {.generation=(uintptr_t)e->user_context};
    if (id == WEBSOCKET_EVENT_CONNECTED) { message.kind = WS_OPEN; xQueueSend(queue, &message, portMAX_DELAY); }
    if (id == WEBSOCKET_EVENT_DISCONNECTED) {
        message.kind = WS_CLOSED; message.code = e->close_status_code; xQueueSend(queue, &message, portMAX_DELAY);
    }
    if (id != WEBSOCKET_EVENT_DATA || (e->op_code != 1 && e->op_code != 0)) return;
    if (e->payload_offset == 0 && e->op_code == 1) received = 0;
    if (e->payload_len > 8192 || received + e->data_len > 8192) { received = -1; return; }
    if (received < 0) return;
    memcpy(frame_buffer + received, e->data_ptr, e->data_len); received += e->data_len;
    if (e->fin && e->payload_offset + e->data_len == e->payload_len) {
        frame_buffer[received] = 0; message.kind = WS_FRAME; message.code = received;
        message.text = strndup(frame_buffer, received); xQueueSend(queue, &message, portMAX_DELAY); received = 0;
    }
}
void hub_send(const char *type, cJSON *data, bool request)
{
    cJSON *root = cJSON_CreateObject(); cJSON_AddNumberToObject(root, "v", 1);
    cJSON_AddStringToObject(root, "t", type); cJSON_AddNumberToObject(root, "ts", time(NULL));
    if (request) cJSON_AddNumberToObject(root, "id", ++request_id);
    cJSON_AddItemToObject(root, "d", data);
    ws_message_t message = {.kind = WS_SEND, .text = cJSON_PrintUnformatted(root)};
    cJSON_Delete(root); xQueueSend(queue, &message, portMAX_DELAY);
}
static void state(void)
{
    cJSON *d = cJSON_CreateObject();
    cJSON_AddBoolToObject(d, "screen", power_screen_on()); cJSON_AddBoolToObject(d, "dnd", nuka_settings.dnd);
    cJSON_AddStringToObject(d, "card", current_card); cJSON_AddItemToObject(d, "bat", battery_json());
    if (net_connected()) cJSON_AddNumberToObject(d, "rssi", net_rssi());
    cJSON_AddNumberToObject(d, "heap_int", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(d, "heap_int_min", heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(d, "up_s", esp_timer_get_time() / 1000000);
    if (battery_low_power()) cJSON_AddBoolToObject(d, "low_power", true);
    hub_send("state", d, false);
}
static void hello(void)
{
    cJSON *d = cJSON_CreateObject(); char device_id[33]; ESP_ERROR_CHECK(store_read_string("prov", "dev_id", device_id, sizeof(device_id)));
    cJSON_AddStringToObject(d, "dev_id", device_id); cJSON_AddStringToObject(d, "fw", esp_app_get_description()->version);
    cJSON_AddStringToObject(d, "nukapack", nuka_assets_version()); cJSON_AddNumberToObject(d, "proto", 1);
    cJSON_AddStringToObject(d, "lang", nuka_settings.lang); cJSON_AddBoolToObject(d, "screen", power_screen_on());
    const char *caps[] = {"imu", "hid", "vault", "audio"}; cJSON_AddItemToObject(d, "caps", cJSON_CreateStringArray(caps, 4));
    const char *topics[] = {"hosts", "servers", "ai", "agents", "notify", "media", "weather", "pet"};
    cJSON_AddItemToObject(d, "topics", cJSON_CreateStringArray(topics, 8)); cJSON_AddItemToObject(d, "bat", battery_json());
    hub_send("hello", d, true);
}
static void post_json(int32_t event_id, const cJSON *data)
{
    char *text = cJSON_PrintUnformatted(data); nuka_post(event_id, text, strlen(text) + 1); cJSON_free(text);
}
static esp_err_t apply_cfg(const cJSON *d)
{
    const cJSON *p; uint16_t mask = nuka_settings.local_overrides;
    p = cJSON_GetObjectItemCaseSensitive(d, "lang"); if (p && !(mask & 1)) strcpy(nuka_settings.lang, p->valuestring);
    p = cJSON_GetObjectItemCaseSensitive(d, "t_dim"); if (p && !(mask & 2)) nuka_settings.t_dim = p->valueint;
    p = cJSON_GetObjectItemCaseSensitive(d, "t_off"); if (p && !(mask & 4)) nuka_settings.t_off = p->valueint;
    p = cJSON_GetObjectItemCaseSensitive(d, "key3_long"); if (p && !(mask & 8)) {
        strcpy(nuka_settings.key3_action, cJSON_GetObjectItemCaseSensitive(p, "action")->valuestring);
        const cJSON *slot = cJSON_GetObjectItemCaseSensitive(p, "slot"); if (slot) nuka_settings.key3_macro = slot->valueint;
    }
    p = cJSON_GetObjectItemCaseSensitive(d, "key3_double_card"); if (p && !(mask & 16)) strcpy(nuka_settings.key3_double_card, p->valuestring);
    p = cJSON_GetObjectItemCaseSensitive(d, "key3_clock_style"); if (p && !(mask & 32)) nuka_settings.clock_style = cJSON_IsTrue(p);
    p = cJSON_GetObjectItemCaseSensitive(d, "home_face"); if (p && !(mask & 64)) strcpy(nuka_settings.home_face,p->valuestring);
    p = cJSON_GetObjectItemCaseSensitive(d, "volume"); if (p && !(mask & 128)) nuka_settings.volume=p->valueint;
    p = cJSON_GetObjectItemCaseSensitive(d, "assistant"); if (p && !(mask & 256)) nuka_settings.assistant=cJSON_IsTrue(p);
    esp_err_t err = store_save_settings(); nuka_post(NUKA_CONFIG_CHANGED, NULL, 0); return err;
}
static esp_err_t handle_frame(const char *text, size_t length)
{
    proto_frame_t f;
    if (!proto_parse(text, length, &f)) return ESP_ERR_INVALID_ARG;
    esp_err_t err = ESP_OK;
    if (f.topic != VM_NONE) vm_store_replace(f.topic, f.data);
    else if (!strcmp(f.type, "welcome")) {
        char expected[33]; err = store_read_string("prov", "hub_id", expected, sizeof(expected));
        if (err == ESP_OK && strcmp(expected, cJSON_GetObjectItemCaseSensitive(f.data, "hub_id")->valuestring)) err = ESP_ERR_INVALID_STATE;
        if (err == ESP_OK) {
            vm_store_new_connection(); connected = true; cJSON_Delete(features);
            features = cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(f.data, "features"), true);
            const cJSON *t = cJSON_GetObjectItemCaseSensitive(f.data, "time");
            err = nuka_time_update(cJSON_GetObjectItemCaseSensitive(t, "epoch")->valuedouble, cJSON_GetObjectItemCaseSensitive(t, "tz_posix")->valuestring);
            const cJSON *ntp = cJSON_GetObjectItemCaseSensitive(f.data, "ntp");
            if (ntp && err == ESP_OK) { const char *servers[CONFIG_LWIP_SNTP_MAX_SERVERS]; unsigned count = cJSON_GetArraySize(ntp); if(count>CONFIG_LWIP_SNTP_MAX_SERVERS)count=CONFIG_LWIP_SNTP_MAX_SERVERS; for (unsigned i = 0; i < count; ++i) servers[i] = cJSON_GetArrayItem(ntp, i)->valuestring; err = nuka_time_sntp(servers, count); }
            if (macro_revision() != cJSON_GetObjectItemCaseSensitive(f.data, "macros_rev")->valuedouble) hub_send("macro.get", cJSON_CreateObject(), true);
            nuka_post(NUKA_HUB_CONNECTED, NULL, 0); ota_hub_ready(); state();
            if(err==ESP_OK)err=store_write_string("sys","last_ip",current_url);
            if(err==ESP_OK)err=store_write_string("sys","last_hub",expected);
        }
    } else if (!strcmp(f.type, "time")) err = nuka_time_update(cJSON_GetObjectItemCaseSensitive(f.data, "epoch")->valuedouble, cJSON_GetObjectItemCaseSensitive(f.data, "tz_posix")->valuestring);
    else if (!strcmp(f.type, "cfg")) err = apply_cfg(f.data);
    else if (!strcmp(f.type, "macro.set")) err = macro_apply(f.data);
    else if (!strcmp(f.type, "perm.req") || !strcmp(f.type, "perm.done")) post_json(NUKA_PERMISSION, f.root);
    else if (!strcmp(f.type, "notify.push")) post_json(NUKA_NOTIFICATION, f.data);
    else if (!strcmp(f.type, "ota.offer")) ota_offer(f.data);
    else if (!strcmp(f.type, "pair.ok")) {
        err = store_write_string("prov", "dev_tok", cJSON_GetObjectItemCaseSensitive(f.data, "dev_token")->valuestring);
        if (err == ESP_OK) err = store_write_string("prov", "hub_id", cJSON_GetObjectItemCaseSensitive(f.data, "hub_id")->valuestring);
        if(err==ESP_OK)esp_websocket_client_close(client,pdMS_TO_TICKS(1000));
    }
    proto_frame_free(&f); return err;
}
static bool origin(const char *url, char out[512])
{
    const char *start = strstr(url, "://"); if (!start) return false;
    const char *end = strchr(start + 3, '/'); size_t length = end ? (size_t)(end - url) : strlen(url);
    size_t extra=!strncmp(url,"ws:",3)?2:!strncmp(url,"wss:",4)?2:0;
    if (length+extra >= 512 || length <= (size_t)(start + 3 - url)) return false;
    memcpy(out, url, length); out[length] = 0;
    if (!strncmp(out, "ws:", 3)) { memmove(out + 4, out + 2, strlen(out + 2) + 1); memcpy(out, "http", 4); }
    else if (!strncmp(out, "wss:", 4)) { memmove(out + 5, out + 3, strlen(out + 3) + 1); memcpy(out, "https", 5); }
    return true;
}
esp_http_client_handle_t hub_http_client(const char *path)
{
    char paired_origin[512], target_origin[512];
    if (!origin(current_url, paired_origin)) return NULL;
    size_t size = strlen(path) + (path[0] == '/' ? strlen(paired_origin) : 0) + 1;
    char *target = malloc(size);
    if (path[0] == '/') snprintf(target, size, "%s%s", paired_origin, path);
    else strcpy(target, path);
    if (!origin(target, target_origin)) { free(target); return NULL; }
    bool allowed = !strcmp(target_origin, paired_origin);
    cJSON *hubs; esp_err_t err = store_read_json("prov", "hubs", &hubs);
    if (err != ESP_OK) { free(target); return NULL; }
    const cJSON *item; cJSON_ArrayForEach(item, hubs) {
        if (!cJSON_IsString(item)) { cJSON_Delete(hubs); free(target); return NULL; }
        char one[512]; if (origin(item->valuestring, one) && !strcmp(one, target_origin)) allowed = true;
    }
    cJSON_Delete(hubs); if (!allowed) { free(target); return NULL; }
    esp_http_client_config_t cfg = {.url = target, .crt_bundle_attach = esp_crt_bundle_attach, .disable_auto_redirect = true, .timeout_ms = 15000};
    esp_http_client_handle_t http = esp_http_client_init(&cfg);
    free(target);
    char token[256]; if (store_read_string("prov", "dev_tok", token, sizeof(token)) == ESP_OK) {
        char auth[264]; snprintf(auth, sizeof(auth), "Bearer %s", token); esp_http_client_set_header(http, "Authorization", auth);
    }
    cJSON *cf; if (store_read_json("prov", "cf_access", &cf) == ESP_OK) {
        const cJSON *o = cJSON_GetObjectItemCaseSensitive(cf, "origin"), *id = cJSON_GetObjectItemCaseSensitive(cf, "client_id"), *secret = cJSON_GetObjectItemCaseSensitive(cf, "client_secret");
        if (cJSON_IsString(o) && cJSON_IsString(id) && cJSON_IsString(secret) && !strcmp(o->valuestring, target_origin)) {
            esp_http_client_set_header(http, "CF-Access-Client-Id", id->valuestring); esp_http_client_set_header(http, "CF-Access-Client-Secret", secret->valuestring);
        }
        cJSON_Delete(cf);
    }
    return http;
}
static esp_err_t connect_to(const char *url)
{
    strcpy(current_url, url);
    esp_websocket_client_config_t cfg = {.uri = url, .disable_auto_reconnect = true, .subprotocol = "nuka.v1",
        .user_context=(void *)(uintptr_t)++generation,
        .crt_bundle_attach = esp_crt_bundle_attach, .task_prio = 4, .task_core_id_set = true, .task_core_id = 0,
        .task_stack = 6144, .buffer_size = 1024, .network_timeout_ms = 5000, .ping_interval_sec = 25, .pingpong_timeout_sec = 50};
    client = esp_websocket_client_init(&cfg);
    char token[256]; esp_err_t err = store_read_string("prov", "dev_tok", token, sizeof(token));
    free(cfg_token); cfg_token = NULL;
    if (err == ESP_OK) { char auth[264]; snprintf(auth, sizeof(auth), "Bearer %s", token); cfg_token = strdup(auth); esp_websocket_client_append_header(client, "Authorization", cfg_token); }
    else if (err == ESP_ERR_NVS_NOT_FOUND && net_pair_code()[0]) {
        char device[33], pair[48]; ESP_ERROR_CHECK(store_read_string("prov", "dev_id", device, sizeof(device)));
        snprintf(pair, sizeof(pair), "%s:%s", device, net_pair_code()); esp_websocket_client_append_header(client, "X-Nuka-Pair", pair);
    } else return err;
    char here[512]; origin(url, here); cJSON *cf;
    if (store_read_json("prov", "cf_access", &cf) == ESP_OK) {
        const cJSON *o = cJSON_GetObjectItemCaseSensitive(cf, "origin"), *id = cJSON_GetObjectItemCaseSensitive(cf, "client_id"), *secret = cJSON_GetObjectItemCaseSensitive(cf, "client_secret");
        if (!cJSON_IsString(o) || !cJSON_IsString(id) || !cJSON_IsString(secret)) { cJSON_Delete(cf); return ESP_ERR_INVALID_ARG; }
        if (!strcmp(o->valuestring, here)) { esp_websocket_client_append_header(client, "CF-Access-Client-Id", id->valuestring); esp_websocket_client_append_header(client, "CF-Access-Client-Secret", secret->valuestring); }
        cJSON_Delete(cf);
    }
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, event, NULL);
    return esp_websocket_client_start(client);
}
static void ws_task(void *arg)
{
    unsigned attempt = 0, index = 0, source=0; int64_t next_connect = 0, last_state = 0;
    bool cooling_down=false;
    unsigned reconnect_cap=30;
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    frame_buffer = malloc(8193);
    for (;;) {
        ESP_ERROR_CHECK(esp_task_wdt_reset());
        esp_err_t network_error=net_poll();if(network_error!=ESP_OK)ESP_LOGE("net","operation: %s",esp_err_to_name(network_error));
        esp_err_t time_error=nuka_time_poll();if(time_error!=ESP_OK)ESP_LOGE("time","synchronization: %s",esp_err_to_name(time_error));
        ws_message_t msg; bool got = xQueueReceive(queue, &msg, pdMS_TO_TICKS(250));
        esp_err_t err = ESP_OK;
        if(atomic_exchange(&reconfigure,false)){stopped=false;connected=false;attempt=index=source=0;next_connect=0;++generation;if(client){esp_websocket_client_stop(client);esp_websocket_client_destroy(client);client=NULL;}nuka_post(NUKA_HUB_DISCONNECTED,NULL,0);}
        if(got&&msg.kind!=WS_SEND&&msg.generation!=generation){if(msg.kind==WS_FRAME)free(msg.text);continue;}
        if (got && msg.kind == WS_OPEN) hello();
        if (got && msg.kind == WS_FRAME) { err = handle_frame(msg.text, msg.code); free(msg.text); }
        if (got && msg.kind == WS_SEND) {
            if (client && esp_websocket_client_is_connected(client)) {
                int n = esp_websocket_client_send_text(client, msg.text, strlen(msg.text), pdMS_TO_TICKS(3000));
                if (n < 0) err = ESP_FAIL;
            }
            free(msg.text);
        }
        if (got && msg.kind == WS_CLOSED) {
            connected = false; nuka_post(NUKA_HUB_DISCONNECTED, NULL, 0);
            if (msg.code == 4001 || msg.code == 4003 || msg.code == 4008) stopped = true;
            if (msg.code == 4004) net_start_provisioning();
            unsigned cap=power_screen_off_seconds()>=1800?120:30,delay=1U<<(attempt<7?attempt++:7);
            if(delay>cap)delay=cap;
            int64_t wait_us=(int64_t)delay*10000*(80+esp_random()%41);
            if(wait_us>cap*1000000LL)wait_us=cap*1000000LL;
            cooling_down=msg.code==4002;
            if(cooling_down)wait_us=600000000LL;
            next_connect = esp_timer_get_time() + wait_us;
            esp_websocket_client_stop(client); esp_websocket_client_destroy(client); client = NULL;
        }
        if (err != ESP_OK) ESP_LOGE("ws", "frame/transport: %s", esp_err_to_name(err));
        int64_t now = esp_timer_get_time();
        unsigned cap=power_screen_off_seconds()>=1800?120:30;
        if(!cooling_down&&cap<reconnect_cap)next_connect=now;
        reconnect_cap=cap;
        if(!cooling_down&&power_screen_off_seconds()<1800&&next_connect>now+30000000LL)
            next_connect=now+30000000LL;
        if (connected) {
            attempt = 0;source=0;index=0; if (atomic_exchange(&state_requested,false)||now - last_state >= 60000000) { state(); last_state = now; }
        }
        if (!client && !stopped && net_connected() && now >= next_connect) {
            char endpoint[512];bool found=false;
            if(source==0){source=1;char expected[33];err=store_read_string("prov","hub_id",expected,sizeof(expected));
                if(err==ESP_OK){mdns_result_t *results=NULL;err=mdns_query_ptr("_nuka","_tcp",1500,8,&results);
                    if(err==ESP_OK){for(mdns_result_t *r=results;r&&!found;r=r->next){bool match=false,tls=false;const char *path="/ws/device";
                        for(unsigned i=0;i<r->txt_count;++i){const char *value=r->txt[i].value;if(!value)continue;if(!strcmp(r->txt[i].key,"id")&&!strcmp(value,expected))match=true;if(!strcmp(r->txt[i].key,"tls"))tls=!strcmp(value,"1");if(!strcmp(r->txt[i].key,"ws"))path=value;}
                        for(mdns_ip_addr_t *ip=r->addr;match&&ip&&!found;ip=ip->next)if(ip->addr.type==ESP_IPADDR_TYPE_V4){int length=snprintf(endpoint,sizeof(endpoint),"%s://" IPSTR ":%u%s",tls?"wss":"ws",IP2STR(&ip->addr.u_addr.ip4),r->port,path);found=length>0&&length<sizeof(endpoint);}
                    }mdns_query_results_free(results);}
                }
            }
            if(!found&&source==1){source=2;char expected[33],cached_hub[33];err=store_read_string("prov","hub_id",expected,sizeof(expected));
                if(err==ESP_OK)err=store_read_string("sys","last_hub",cached_hub,sizeof(cached_hub));
                if(err==ESP_OK&&!strcmp(expected,cached_hub)){err=store_read_string("sys","last_ip",endpoint,sizeof(endpoint));found=err==ESP_OK&&(!strncmp(endpoint,"ws://",5)||!strncmp(endpoint,"wss://",6));}
            }
            if(found){err=connect_to(endpoint);if(err!=ESP_OK){ESP_LOGE("ws","discovery connect: %s",esp_err_to_name(err));if(client){esp_websocket_client_destroy(client);client=NULL;}next_connect=now+1000000;}continue;}
            cJSON *hubs; err = store_read_json("prov", "hubs", &hubs);
            if (err == ESP_ERR_NVS_NOT_FOUND) { next_connect = now + 1000000; continue; }
            if (err == ESP_OK) {
                if (!cJSON_IsArray(hubs) || !cJSON_GetArraySize(hubs)) err = ESP_ERR_INVALID_ARG;
                else {
                    const cJSON *url = cJSON_GetArrayItem(hubs, index++ % cJSON_GetArraySize(hubs));
                    if(index>=cJSON_GetArraySize(hubs)){index=0;source=0;}
                    if (!cJSON_IsString(url) || strlen(url->valuestring) >= sizeof(current_url) || (strncmp(url->valuestring, "ws://", 5) && strncmp(url->valuestring, "wss://", 6))) err = ESP_ERR_INVALID_ARG;
                    else err = connect_to(url->valuestring);
                }
                cJSON_Delete(hubs);
            }
            if (err != ESP_OK) { ESP_LOGE("ws", "connect: %s", esp_err_to_name(err)); if (client) { esp_websocket_client_destroy(client); client = NULL; } next_connect = now + 5000000; }
        }
    }
}
void hub_client_init(void) { queue=xQueueCreate(24,sizeof(ws_message_t));ESP_ERROR_CHECK(queue?ESP_OK:ESP_ERR_NO_MEM); }
void hub_client_start(void) { ESP_ERROR_CHECK(xTaskCreatePinnedToCore(ws_task,"net",6144,NULL,5,NULL,0)==pdPASS?ESP_OK:ESP_ERR_NO_MEM); }
bool hub_connected(void) { return connected; }
void hub_disconnect(void) { stopped = true; if (client) esp_websocket_client_close(client, pdMS_TO_TICKS(1000)); }
void hub_reconfigure(void) { atomic_store(&reconfigure,true); }
void hub_request_state(void) { atomic_store(&state_requested,true); }
void hub_subscribe_detail(const char *topic, const char *id)
{
    cJSON *d = cJSON_CreateObject(); if (topic) { cJSON_AddStringToObject(d, "topic", topic); cJSON_AddStringToObject(d, "id", id); }
    else cJSON_AddNullToObject(d, "topic");
    hub_send("sub.detail", d, false);
}
bool hub_feature_enabled(const char *topic) { const cJSON *v = cJSON_GetObjectItemCaseSensitive(features, topic); return !v || cJSON_IsTrue(v); }
void hub_set_feature(const char *topic, bool enabled)
{
    vm_topic_t selected=vm_topic_from_name(topic);if(selected!=VM_NONE)subscribed[selected]=enabled;
    cJSON *d = cJSON_CreateObject(), *topics = cJSON_AddArrayToObject(d, "topics");
    const char *names[] = {"hosts","servers","ai","agents","notify","media","weather","pet"};
    for (unsigned i = 0; i < 8; ++i) if (subscribed[vm_topic_from_name(names[i])]) cJSON_AddItemToArray(topics, cJSON_CreateString(names[i]));
    hub_send("sub", d, false);
}
void hub_set_card(const char *card) { strcpy(current_card, card); if (connected) state(); }
