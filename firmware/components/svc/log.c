// SPDX-License-Identifier: AGPL-3.0-or-later
#include "log.h"
#include "diag.h"
#include "usb.h"
#include "esp_log.h"
#include "esp_littlefs.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#define RING_SIZE 65536
typedef struct { char line[384]; time_t at; } log_entry_t;
static QueueHandle_t entries;
static StaticQueue_t entries_control;
static SemaphoreHandle_t file_lock;
static vprintf_like_t previous;
static FILE *ring;
static uint8_t *ring_data;
static uint32_t position, filled;
static bool streaming;
static int capture(const char *format, va_list args)
{
    va_list copy; va_copy(copy, args); int result = previous(format, copy); va_end(copy);
    log_entry_t entry = {.at = time(NULL)};
    vsnprintf(entry.line, sizeof(entry.line), format, args);
    xQueueSend(entries, &entry, 0); return result;
}
void nuka_log_init(void)
{
    uint8_t *storage=heap_caps_malloc(24*sizeof(log_entry_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK(storage?ESP_OK:ESP_ERR_NO_MEM);
    entries = xQueueCreateStatic(24,sizeof(log_entry_t),storage,&entries_control);
    file_lock = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(entries&&file_lock?ESP_OK:ESP_ERR_NO_MEM);
    previous = esp_log_set_vprintf(capture);
}
static void log_task(void *arg)
{
    log_entry_t entry;
    for (;;) {
        xQueueReceive(entries, &entry, portMAX_DELAY);
        int64_t deadline=esp_timer_get_time()+1000000;
        for(;;) {
            size_t length = strlen(entry.line);
            xSemaphoreTake(file_lock, portMAX_DELAY);
            size_t first = length < RING_SIZE - position ? length : RING_SIZE - position;
            memcpy(ring_data+position,entry.line,first);
            if(first<length)memcpy(ring_data,entry.line+first,length-first);
            position=(position+length)%RING_SIZE;filled=filled+length>RING_SIZE?RING_SIZE:filled+length;
            xSemaphoreGive(file_lock);
            if (streaming) {
                char *text = entry.line;
                while (*text == '\033') { char *end = strchr(text, 'm'); text = end ? end + 1 : text; if (!end) break; }
                char level[2] = {*text, 0};
                if (!strchr("EWIDV", *text)) level[0] = 'I';
                char *tag = strstr(text, ") "); tag = tag ? tag + 2 : text;
                char *colon = strchr(tag, ':');
                if (colon) *colon = 0;
                cJSON *d = cJSON_CreateObject(); cJSON_AddStringToObject(d, "lvl", level);
                cJSON_AddStringToObject(d, "tag", colon ? tag : "log");
                cJSON_AddStringToObject(d, "msg", colon ? colon + 1 : text);
                cJSON_AddNumberToObject(d, "ts", entry.at); usb_send("log", -1, d); cJSON_Delete(d);
            }
            int64_t remaining=deadline-esp_timer_get_time();
            if(remaining<=0||!xQueueReceive(entries,&entry,pdMS_TO_TICKS((remaining+999)/1000)))break;
        }
        /* One sequential replacement avoids two LittleFS copy-on-writes per log line. */
        bool written=fseek(ring,0,SEEK_SET)==0;
        written&=fwrite(&position,4,1,ring)==1&&fwrite(&filled,4,1,ring)==1;
        written&=fwrite(ring_data,1,RING_SIZE,ring)==RING_SIZE;
        written&=fflush(ring)==0;
        written&=fsync(fileno(ring))==0;
        if(!written){fputs("E logger: storage write failed\n",stderr);vTaskDelete(NULL);}
    }
}
void diag_start(void)
{
    esp_vfs_littlefs_conf_t config = {.base_path = "/storage", .partition_label = "storage", .format_if_mount_failed = true};
    ESP_ERROR_CHECK(esp_vfs_littlefs_register(&config));
    ring_data=heap_caps_calloc(1,RING_SIZE,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK(ring_data?ESP_OK:ESP_ERR_NO_MEM);
    ring = fopen("/storage/log", "r+b");
    if (!ring) {
        ESP_ERROR_CHECK(errno==ENOENT?ESP_OK:ESP_FAIL);
        FILE *created = fopen("/storage/log.new", "w+b"); ESP_ERROR_CHECK(created ? ESP_OK : ESP_FAIL);
        bool written = fwrite(&position, 4, 1, created) == 1 && fwrite(&filled, 4, 1, created) == 1;
        written &= fwrite(ring_data,1,RING_SIZE,created)==RING_SIZE&&fflush(created)==0;
        written &= fclose(created) == 0;
        ESP_ERROR_CHECK(written ? ESP_OK : ESP_FAIL);
        ESP_ERROR_CHECK(rename("/storage/log.new", "/storage/log") == 0 ? ESP_OK : ESP_FAIL);
        ring = fopen("/storage/log", "r+b"); ESP_ERROR_CHECK(ring ? ESP_OK : ESP_FAIL);
    } else {
        bool metadata = fread(&position, 4, 1, ring) == 1 && fread(&filled, 4, 1, ring) == 1;
        ESP_LOGI("logger", "cursor=%lu used=%lu header=%u", (unsigned long)position, (unsigned long)filled, metadata);
        ESP_ERROR_CHECK(metadata && position < RING_SIZE && filled <= RING_SIZE ? ESP_OK : ESP_ERR_INVALID_STATE);
        ESP_ERROR_CHECK(fread(ring_data,1,RING_SIZE,ring)==RING_SIZE?ESP_OK:ESP_ERR_INVALID_SIZE);
    }
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(log_task,"logger",3072,NULL,1,NULL,0)==pdPASS?ESP_OK:ESP_ERR_NO_MEM);
}
void diag_stream(bool enabled) { streaming = enabled; }
esp_err_t diag_dump(uint8_t **data, size_t *length)
{
    xSemaphoreTake(file_lock, portMAX_DELAY);
    *length = filled; *data = malloc(filled);
    uint32_t start = filled == RING_SIZE ? position : 0;
    size_t first = filled < RING_SIZE - start ? filled : RING_SIZE - start;
    memcpy(*data,ring_data+start,first);
    if(first<filled)memcpy(*data+first,ring_data,filled-first);
    xSemaphoreGive(file_lock); return ESP_OK;
}
