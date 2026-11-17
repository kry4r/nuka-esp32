// SPDX-License-Identifier: AGPL-3.0-or-later
#include "media_cover.h"
#include "hub_client.h"
#include "jpeg_decoder.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <stdlib.h>
#include <string.h>
typedef struct { char *path; uint8_t *pixels; } cover_result_t;
static char *requested,*loaded;
static QueueHandle_t downloads,completed;
static uint8_t *pixels;
static lv_image_dsc_t image;
static void download_task(void *arg)
{
    char *path;
    while(xQueueReceive(downloads,&path,portMAX_DELAY)){
        esp_err_t err=ESP_OK;esp_http_client_handle_t http=hub_http_client(path);
        if(!http)err=ESP_ERR_INVALID_ARG;
        uint8_t *jpeg=heap_caps_malloc(20481,MALLOC_CAP_SPIRAM);int size=0;
        if(err==ESP_OK)err=esp_http_client_open(http,0);
        if(err==ESP_OK){int64_t length=esp_http_client_fetch_headers(http);if(length>20480||esp_http_client_get_status_code(http)!=200)err=ESP_ERR_INVALID_SIZE;}
        while(err==ESP_OK&&size<20481){int n=esp_http_client_read(http,(char *)jpeg+size,20481-size);if(n<0){err=ESP_FAIL;break;}if(n==0)break;size+=n;}
        if(size>20480)err=ESP_ERR_INVALID_SIZE;
        if(http){esp_http_client_close(http);esp_http_client_cleanup(http);}
        uint8_t *output=NULL;
        if(err==ESP_OK){
            esp_jpeg_image_cfg_t cfg={.indata=jpeg,.indata_size=size,.out_format=JPEG_IMAGE_FORMAT_RGB565,.out_scale=JPEG_IMAGE_SCALE_0};
            esp_jpeg_image_output_t out;err=esp_jpeg_get_image_info(&cfg,&out);
            if(err==ESP_OK&&(out.width!=160||out.height!=160))err=ESP_ERR_INVALID_SIZE;
            if(err==ESP_OK){output=heap_caps_malloc(160*160*2,MALLOC_CAP_SPIRAM);cfg.outbuf=output;cfg.outbuf_size=160*160*2;err=esp_jpeg_decode(&cfg,&out);}
        }
        heap_caps_free(jpeg);
        if(err==ESP_OK){cover_result_t result={path,output};xQueueSend(completed,&result,portMAX_DELAY);}
        else{free(path);heap_caps_free(output);ESP_LOGE("media","cover: %s",esp_err_to_name(err));}
    }
}
const lv_image_dsc_t *media_cover(const char *path)
{
    if(!downloads){downloads=xQueueCreate(1,sizeof(char *));completed=xQueueCreate(1,sizeof(cover_result_t));xTaskCreatePinnedToCore(download_task,"cover",4096,NULL,2,NULL,0);}
    cover_result_t result;
    if(xQueueReceive(completed,&result,0)){free(loaded);heap_caps_free(pixels);loaded=result.path;pixels=result.pixels;image=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=160,.h=160,.stride=320},.data_size=160*160*2,.data=pixels};}
    if(!requested||strcmp(requested,path)){
        free(requested);requested=strdup(path);char *pending;
        if(xQueueReceive(downloads,&pending,0))free(pending);
        pending=strdup(path);xQueueSend(downloads,&pending,0);
    }
    return loaded&&!strcmp(loaded,path)?&image:NULL;
}
