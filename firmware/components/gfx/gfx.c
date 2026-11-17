// SPDX-License-Identifier: AGPL-3.0-or-later
#include "gfx.h"
#include <math.h>
#include "lvgl.h"
#include "co5300_panel.h"
#include "cst9220_touch.h"
#include "power.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
static esp_lcd_panel_handle_t panel;
static esp_lcd_panel_io_handle_t io;
static SemaphoreHandle_t flushed, ready, paused, panel_lock;
static TaskHandle_t ui_task;
static volatile bool suspended, pause_requested;
static uint8_t brightness;
static uint32_t flushes,frames;
static bool drop_touch;
static esp_err_t runtime_error;
static void (*create_ui)(void), (*tick_ui)(void);
static void (*touch_observer)(bool,int,int);
static bool color_done(esp_lcd_panel_io_handle_t handle, esp_lcd_panel_io_event_data_t *event, void *ctx)
{
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(flushed, &wake);
    return wake == pdTRUE;
}
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *data)
{
    size_t pixels = lv_area_get_width(area) * lv_area_get_height(area);
    lv_draw_sw_rgb565_swap(data, pixels);
    /* The panel IO transaction pool is shared with power-task commands. */
    xSemaphoreTake(panel_lock, portMAX_DELAY);
    runtime_error = esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1,
        area->x2 + 1, area->y2 + 1, data);
    if (runtime_error == ESP_OK) {
        xSemaphoreTake(flushed, portMAX_DELAY); ++flushes;
        if(lv_display_flush_is_last(display))++frames;
    }
    xSemaphoreGive(panel_lock);
    if (flushes == 1) ESP_LOGI("gfx", "first DMA completed area=%d,%d-%d,%d", area->x1, area->y1, area->x2, area->y2);
    lv_display_flush_ready(display);
}
static void round_area(lv_event_t *event)
{
    lv_area_t *a = lv_event_get_param(event);
    a->x1 &= ~1; a->y1 &= ~1; a->x2 |= 1; a->y2 |= 1;
}
static void pointer_read(lv_indev_t *input, lv_indev_data_t *data)
{
    static lv_point_t point;
    touch_sample_t touch;
    esp_err_t err = touch_read(&touch);
    if (err != ESP_OK) { runtime_error=err; return; }
    if (drop_touch && !touch.pressed) drop_touch = false;
    if (touch.pressed) { point.x = touch.x; point.y = touch.y; power_activity(); }
    if (drop_touch) touch.pressed = false;
    if (touch_observer) touch_observer(touch.pressed,point.x,point.y);
    data->state = touch.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->point = point;
}
static uint32_t tick_ms(void) { return esp_timer_get_time() / 1000; }
static void lvgl_task(void *arg)
{
    lv_init(); lv_tick_set_cb(tick_ms);
    lv_display_t *display = lv_display_create(480, 480);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    void *buf1 = heap_caps_malloc(480 * 48 * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    void *buf2 = heap_caps_malloc(480 * 48 * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    assert(buf1 && buf2);
    lv_display_set_buffers(display, buf1, buf2, 480 * 48 * 2, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    lv_display_add_event_cb(display, round_area, LV_EVENT_INVALIDATE_AREA, NULL);
    lv_indev_t *pointer = lv_indev_create();
    lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER); lv_indev_set_read_cb(pointer, pointer_read);
    create_ui();
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    xSemaphoreGive(ready);
    for (;;) {
        ESP_ERROR_CHECK(esp_task_wdt_reset());
        if (pause_requested && !suspended) { suspended = true; xSemaphoreGive(paused); }
        tick_ui();
        if (suspended) { ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(250)); continue; }
        uint32_t delay = lv_timer_handler();
        if(runtime_error!=ESP_OK){ESP_LOGE("gfx","display/input: %s",esp_err_to_name(runtime_error));runtime_error=ESP_OK;}
        vTaskDelay(pdMS_TO_TICKS(delay > 16 ? 16 : delay == 0 ? 1 : delay));
    }
}
esp_err_t gfx_init(void)
{
    flushed = xSemaphoreCreateBinary(); ready = xSemaphoreCreateBinary(); paused = xSemaphoreCreateBinary();
    panel_lock = xSemaphoreCreateMutex();
    assert(flushed && ready && paused && panel_lock);
    ESP_ERROR_CHECK(co5300_panel_init(&panel, &io));
    esp_lcd_panel_io_callbacks_t cb = {.on_color_trans_done = color_done};
    return esp_lcd_panel_io_register_event_callbacks(io, &cb, NULL);
}
static esp_err_t panel_command(uint8_t command, const void *data, size_t size)
{
    xSemaphoreTake(panel_lock, portMAX_DELAY);
    esp_err_t error = co5300_command(command, data, size);
    xSemaphoreGive(panel_lock);
    return error;
}
void gfx_start(void (*bootstrap)(void), void (*tick)(void))
{
    create_ui = bootstrap; tick_ui = tick;
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(lvgl_task,"lvgl",8192,NULL,4,&ui_task,1)==pdPASS?ESP_OK:ESP_ERR_NO_MEM);
    xSemaphoreTake(ready, portMAX_DELAY);
}
esp_err_t gfx_set_brightness(uint8_t pct)
{
    brightness = pct;
    uint8_t level = pct == 0 ? 0 : 8 + (uint8_t)(247 * powf(pct / 100.0f, 2.2f));
    return panel_command(0x51, &level, 1);
}
esp_err_t gfx_fade_to(uint8_t pct, uint32_t ms)
{
    int start = brightness;
    for (uint32_t elapsed = 0; elapsed < ms; elapsed += 10) {
        esp_err_t error = gfx_set_brightness(start + ((int)pct - start) * elapsed / ms);
        if(error!=ESP_OK)return error;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGI("gfx", "panel brightness pct=%u", pct);
    return gfx_set_brightness(pct);
}
esp_err_t gfx_suspend(void)
{
    esp_err_t error=gfx_fade_to(0, 200);if(error!=ESP_OK)return error;pause_requested = true;
    xSemaphoreTake(paused, portMAX_DELAY);
    error=panel_command(0x28, NULL, 0);
    return error==ESP_OK?panel_command(0x10, NULL, 0):error;
}
esp_err_t gfx_resume(void)
{
    esp_err_t error=panel_command(0x11, NULL, 0);if(error!=ESP_OK)return error;vTaskDelay(pdMS_TO_TICKS(120));
    error=panel_command(0x29, NULL, 0);if(error!=ESP_OK)return error;drop_touch = true; pause_requested = suspended = false;
    xTaskNotifyGive(ui_task);
    return ESP_OK;
}
uint32_t gfx_flush_count(void) { return flushes; }
uint32_t gfx_frame_count(void) { return frames; }
void gfx_set_touch_observer(void (*observer)(bool,int,int)) { touch_observer=observer; }
void *lv_malloc_core(size_t size) { return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
void *lv_realloc_core(void *ptr, size_t size) { return heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
void lv_free_core(void *ptr) { heap_caps_free(ptr); }
void lv_mem_init(void) {}
void lv_mem_deinit(void) {}
void lv_mem_monitor_core(lv_mem_monitor_t *monitor)
{
    multi_heap_info_t info; heap_caps_get_info(&info, MALLOC_CAP_SPIRAM);
    *monitor = (lv_mem_monitor_t){.total_size = info.total_free_bytes + info.total_allocated_bytes,
        .free_cnt = info.free_blocks, .free_size = info.total_free_bytes,
        .free_biggest_size = info.largest_free_block, .used_cnt = info.allocated_blocks,
        .max_used = info.total_allocated_bytes};
}
lv_result_t lv_mem_test_core(void)
{
    return heap_caps_check_integrity(MALLOC_CAP_SPIRAM, false) ? LV_RESULT_OK : LV_RESULT_INVALID;
}
