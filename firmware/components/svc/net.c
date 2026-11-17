// SPDX-License-Identifier: AGPL-3.0-or-later
#include "net.h"
#include "store.h"
#include "hub_client.h"
#include "proto.h"
#include "nuka_events.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_random.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "mdns.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
enum { NET_STA_START, NET_DISCONNECTED, NET_GOT_IP, NET_RELOAD, NET_PROVISION };
static QueueHandle_t commands;
static atomic_bool connected;
static atomic_int rssi;
static bool starting, provisioning;
static unsigned network_index, failures;
static char pairing[7], provision_ssid[33], provision_url[64];
static httpd_handle_t server;
static const char page[] =
"<!doctype html><html lang=zh-CN><meta charset=utf-8><meta name=viewport content='width=device-width'>"
"<title>nuka 配网</title><style>body{background:#0c1016;color:#eef2f7;font:18px system-ui;max-width:32rem;margin:3rem auto;padding:1rem}input,button{box-sizing:border-box;width:100%;padding:12px;margin:8px 0;background:#202936;color:inherit;border:1px solid #738396;border-radius:8px}button{background:#6f9652}</style>"
"<h1>nuka 配网</h1><form method=post action=/prov>"
"<label>Wi-Fi 名称<input name=ssid required maxlength=32></label>"
"<label>Wi-Fi 密码<input name=pass type=password maxlength=63></label>"
"<label>Hub WebSocket 地址<input name=hub required maxlength=511 placeholder='ws://192.0.2.1:8790/ws/device'></label>"
"<details><summary>Cloudflare Access 服务令牌</summary>"
"<input name=cf_origin placeholder='https://hub.example.com'><input name=cf_client_id placeholder='Client ID'><input name=cf_client_secret type=password placeholder='Client secret'></details>"
"<button>保存并连接</button></form><p>连网后，在 Hub 网页输入设备屏幕上的配对码。</p></html>";
static esp_err_t connect_next(void)
{
    for (unsigned tried = 0; tried < 3; ++tried) {
        char key[8]; snprintf(key, sizeof(key), "wifi%u", network_index);
        cJSON *wifi; esp_err_t err = store_read_json("prov", key, &wifi);
        if (err == ESP_ERR_NVS_NOT_FOUND) { network_index = (network_index + 1) % 3; continue; }
        if (err != ESP_OK) return err;
        const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(wifi, "ssid");
        const cJSON *pass = cJSON_GetObjectItemCaseSensitive(wifi, "pass");
        if (!cJSON_IsString(ssid) || !cJSON_IsString(pass) || strlen(ssid->valuestring) > 32 ||
            strlen(pass->valuestring) > 63 || !ssid->valuestring[0]) {
            cJSON_Delete(wifi); return ESP_ERR_INVALID_ARG;
        }
        wifi_config_t cfg = {.sta.listen_interval = 3};
        memcpy(cfg.sta.ssid, ssid->valuestring, strlen(ssid->valuestring));
        memcpy(cfg.sta.password, pass->valuestring, strlen(pass->valuestring));
        cJSON_Delete(wifi);
        err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
        if (err != ESP_OK) return err;
        return esp_wifi_connect();
    }
    ESP_LOGI("net", "Wi-Fi is not configured");
    return ESP_OK;
}
static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    int command;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) command = NET_STA_START;
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) command = NET_DISCONNECTED;
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) command = NET_GOT_IP;
    else return;
    xQueueSend(commands, &command, portMAX_DELAY);
}
static esp_err_t get_page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}
static bool decode(char *text)
{
    char *out = text;
    for (char *p = text; *p; ++p) {
        if (*p == '+') *out++ = ' ';
        else if (*p == '%') {
            if (!isxdigit((unsigned char)p[1]) || !p[2] || !isxdigit((unsigned char)p[2])) return false;
            char hex[3] = {p[1], p[2], 0}; char decoded = strtol(hex, NULL, 16);
            if (!decoded) return false;
            *out++ = decoded; p += 2;
        } else *out++ = *p;
    }
    *out = 0; return true;
}
static bool access_origin(const char *text)
{
    if (strncmp(text, "https://", 8)) return false;
    const char *p = text + 8, *host = p;
    while (isalnum((unsigned char)*p) || *p == '.' || *p == '-') ++p;
    if (p == host) return false;
    if (*p == ':') {
        ++p; const char *port = p;
        while (isdigit((unsigned char)*p)) ++p;
        if (p == port) return false;
    }
    return !*p;
}
static bool hub_address(const char *text)
{
    if (strlen(text) > 511 || (strncmp(text, "ws://", 5) && strncmp(text, "wss://", 6))) return false;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
        if (*p <= 32 || *p >= 127) return false;
    return true;
}
static esp_err_t clear_pairing(bool access)
{
    nvs_handle_t handle; esp_err_t error = nvs_open("prov", NVS_READWRITE, &handle);
    if (error != ESP_OK) return error;
    const char *keys[] = {"hub_id", "hub_name", "dev_tok", "cf_access"};
    for (unsigned i = 0; i < (access ? 3 : 4) && error == ESP_OK; ++i) {
        error = nvs_erase_key(handle, keys[i]);
        if (error == ESP_ERR_NVS_NOT_FOUND) error = ESP_OK;
    }
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle); return error;
}
static esp_err_t post_prov(httpd_req_t *req)
{
    esp_err_t error = ESP_OK;
    if (req->content_len == 0 || req->content_len > 4096) goto bad;
    char *body = malloc(req->content_len + 1); size_t used = 0;
    if (!body) { error = ESP_ERR_NO_MEM; goto failed; }
    while (used < req->content_len) {
        int count = httpd_req_recv(req, body + used, req->content_len - used);
        if (count <= 0) { free(body); goto bad; } used += count;
    }
    if (!proto_json_text_valid(body, used)) { free(body); goto bad; }
    body[used] = 0;
    const char *names[] = {"ssid", "pass", "hub", "cf_origin", "cf_client_id", "cf_client_secret"};
    char *values[6] = {0}; char *save;
    for (char *part = strtok_r(body, "&", &save); part; part = strtok_r(NULL, "&", &save)) {
        char *equal = strchr(part, '='); if (!equal) { free(body); goto bad; }
        *equal++ = 0; if (!decode(part) || !decode(equal)) { free(body); goto bad; }
        for (unsigned i = 0; i < 6; ++i) if (strcmp(part, names[i]) == 0) values[i] = equal;
    }
    if (!values[0] || !values[0][0] || strlen(values[0]) > 32 || !values[1] || strlen(values[1]) > 63 ||
        !values[2] || !hub_address(values[2])) { free(body); goto bad; }
    unsigned access = 0;
    for (unsigned i = 3; i < 6; ++i) access += values[i] && values[i][0];
    if ((access != 0 && access != 3) || (access && !access_origin(values[3]))) { free(body); goto bad; }
    error = clear_pairing(access != 0);
    if (error != ESP_OK) { free(body); goto failed; }
    cJSON *wifi = cJSON_CreateObject(); cJSON_AddStringToObject(wifi, "ssid", values[0]); cJSON_AddStringToObject(wifi, "pass", values[1]);
    error = store_write_json("prov", "wifi0", wifi); cJSON_Delete(wifi);
    if (error != ESP_OK) { free(body); goto failed; }
    cJSON *hubs = cJSON_CreateArray(); cJSON_AddItemToArray(hubs, cJSON_CreateString(values[2]));
    error = store_write_json("prov", "hubs", hubs); cJSON_Delete(hubs);
    if (error != ESP_OK) { free(body); goto failed; }
    if (access) {
        cJSON *cf = cJSON_CreateObject(); cJSON_AddStringToObject(cf, "origin", values[3]);
        cJSON_AddStringToObject(cf, "client_id", values[4]); cJSON_AddStringToObject(cf, "client_secret", values[5]);
        error = store_write_json("prov", "cf_access", cf); cJSON_Delete(cf);
        if (error != ESP_OK) { free(body); goto failed; }
    }
    free(body); httpd_resp_set_type(req, "application/json");
    error = httpd_resp_sendstr(req, "{}"); net_reload(); return error;
bad:
    httpd_resp_set_status(req, "400 Bad Request"); httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"error\":{\"code\":\"bad_request\",\"message\":\"Invalid provisioning form\"}}");
failed:
    ESP_LOGE("net", "Provisioning storage: %s", esp_err_to_name(error));
    httpd_resp_set_status(req, "500 Internal Server Error"); httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"error\":{\"code\":\"internal\",\"message\":\"Could not save provisioning\"}}");
}
void net_start(void)
{
    commands = xQueueCreate(16, sizeof(int));
    ESP_ERROR_CHECK(commands ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(esp_netif_init()); ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta(); esp_netif_t *ap = esp_netif_create_default_wifi_ap();
    uint8_t mac[6]; ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP));
    snprintf(provision_ssid, sizeof(provision_ssid), "Nuka-%02X%02X", mac[4], mac[5]);
    esp_netif_ip_info_t ip; ESP_ERROR_CHECK(esp_netif_get_ip_info(ap, &ip));
    snprintf(provision_url, sizeof(provision_url), "http://" IPSTR "/", IP2STR(&ip.ip));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT(); ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM)); ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(mdns_init()); ESP_ERROR_CHECK(esp_wifi_start());
}
void net_reload(void)
{
    int command = NET_RELOAD; xQueueSend(commands, &command, portMAX_DELAY);
}
void net_start_provisioning(void)
{
    int command = NET_PROVISION; xQueueSend(commands, &command, portMAX_DELAY);
}
static esp_err_t start_provisioning(void)
{
    snprintf(pairing, sizeof(pairing), "%06lu", (unsigned long)(esp_random() % 1000000));
    wifi_config_t ap = {.ap.channel = 1, .ap.max_connection = 2, .ap.authmode = WIFI_AUTH_OPEN};
    strcpy((char *)ap.ap.ssid, provision_ssid);
    ap.ap.ssid_len = strlen((char *)ap.ap.ssid);
    esp_err_t error = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (error == ESP_OK) error = esp_wifi_set_config(WIFI_IF_AP, &ap);
    if (error != ESP_OK) return error;
    if (!provisioning) {
        httpd_config_t config = HTTPD_DEFAULT_CONFIG(); error = httpd_start(&server, &config);
        if (error != ESP_OK) return error;
        httpd_uri_t get = {.uri = "/", .method = HTTP_GET, .handler = get_page};
        httpd_uri_t post = {.uri = "/prov", .method = HTTP_POST, .handler = post_prov};
        error = httpd_register_uri_handler(server, &get);
        if (error == ESP_OK) error = httpd_register_uri_handler(server, &post);
        if (error != ESP_OK) { httpd_stop(server); server = NULL; return error; }
        provisioning = true;
    }
    nuka_post(NUKA_CONFIG_CHANGED, NULL, 0);
    return ESP_OK;
}
esp_err_t net_poll(void)
{
    int command;
    while (xQueueReceive(commands, &command, 0)) {
        esp_err_t error = ESP_OK;
        if (command == NET_STA_START) { starting = true; error = connect_next(); }
        else if (command == NET_DISCONNECTED) {
            connected = false; rssi = 0;
            if (starting) {
                if (++failures >= 3) { failures = 0; network_index = (network_index + 1) % 3; }
                error = connect_next();
            }
        } else if (command == NET_GOT_IP) { connected = true; failures = 0; }
        else if (command == NET_RELOAD) {
            starting = false; connected = false; rssi = 0; network_index = failures = 0;
            hub_reconfigure();
            error = esp_wifi_stop();
            if (error == ESP_OK) error = esp_wifi_start();
        } else if (command == NET_PROVISION) {
            error = start_provisioning();
            if (error == ESP_OK) { hub_reconfigure(); nuka_post(NUKA_PROVISIONING_READY,NULL,0); }
        }
        if (error != ESP_OK) return error;
    }
    if (connected) {
        wifi_ap_record_t ap; esp_err_t error = esp_wifi_sta_get_ap_info(&ap);
        if (error == ESP_ERR_WIFI_NOT_CONNECT) { connected = false; rssi = 0; }
        else if (error != ESP_OK) return error;
        else rssi = ap.rssi;
    }
    return ESP_OK;
}
bool net_connected(void) { return connected; }
int net_rssi(void) { return rssi; }
const char *net_pair_code(void) { return pairing; }
const char *net_provision_ssid(void) { return provision_ssid; }
const char *net_provision_url(void) { return provision_url; }
