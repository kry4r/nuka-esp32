// SPDX-License-Identifier: AGPL-3.0-or-later
#include "audio_spike.h"
#include "sdkconfig.h"
#ifdef CONFIG_NUKA_AUDIO_SPIKE
#include "audio.h"
#include "net.h"
#include "power.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "opus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include <limits.h>
#include <string.h>
#include <stdlib.h>

enum { SAMPLES=80000, FRAME=960, STACK=CONFIG_NUKA_AUDIO_SPIKE_STAGE==4?65536:16384 };
typedef struct { uint32_t idle[2], task; int64_t at; } cpu_sample_t;
static cpu_sample_t cpu_sample(void)
{
    cpu_sample_t sample={.at=esp_timer_get_time()};
    UBaseType_t count=uxTaskGetNumberOfTasks();
    TaskStatus_t *tasks=malloc(count*sizeof(*tasks));
    count=uxTaskGetSystemState(tasks,count,NULL);
    for(unsigned i=0;i<count;++i){
        if(!strcmp(tasks[i].pcTaskName,"IDLE0"))sample.idle[0]=tasks[i].ulRunTimeCounter;
        if(!strcmp(tasks[i].pcTaskName,"IDLE1"))sample.idle[1]=tasks[i].ulRunTimeCounter;
        if(tasks[i].xHandle==xTaskGetCurrentTaskHandle())sample.task=tasks[i].ulRunTimeCounter;
    }
    free(tasks);return sample;
}
static void metrics(const char *codec,unsigned mode,cpu_sample_t start,size_t minimum,size_t payload,int peak,int64_t encode_us,int64_t decode_us)
{
    vTaskDelay(1);
    cpu_sample_t end=cpu_sample();
    int64_t duration=end.at-start.at;
    uint64_t idle=(uint32_t)(end.idle[0]-start.idle[0])+(uint32_t)(end.idle[1]-start.idle[1]);
    ESP_LOGI("audio-spike","codec=%s ps=%u wall_us=%lld task_cpu_us=%lu system_cpu_pct=%.1f heap_int_min=%u psram_free=%u stack_free=%u payload_b=%u peak=%d encode_us=%lld decode_us=%lld",
        codec,mode,(long long)duration,(unsigned long)(end.task-start.task),
        100.0f-(double)idle*50.0/duration,(unsigned)minimum,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),(unsigned)uxTaskGetStackHighWaterMark(NULL),
        (unsigned)payload,peak,(long long)encode_us,(long long)decode_us);
}
static esp_err_t record(int16_t *recording,size_t *minimum,int *peak)
{
    for(unsigned offset=0;offset<SAMPLES;offset+=160){
        esp_err_t error=audio_read(recording+offset,160);if(error!=ESP_OK)return error;
        size_t available=heap_caps_get_free_size(MALLOC_CAP_INTERNAL);if(available<*minimum)*minimum=available;
        for(unsigned i=0;i<160;++i){int magnitude=abs(recording[offset+i]);if(magnitude>*peak)*peak=magnitude;}
    }
    return ESP_OK;
}
static void spike(void *arg)
{
    while(!net_connected())vTaskDelay(pdMS_TO_TICKS(250));
    vTaskDelay(pdMS_TO_TICKS(10000));
    if(CONFIG_NUKA_AUDIO_SPIKE_STAGE==1){
        power_activity();
        ESP_LOGI("audio-spike","tone begin: one 500 ms synthesized tone; no recording or Opus");
        esp_err_t error=audio_tone();
        ESP_LOGI("audio-spike","tone end: %s heap_int=%u stack_free=%u",esp_err_to_name(error),
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),(unsigned)uxTaskGetStackHighWaterMark(NULL));
        vTaskDeleteWithCaps(NULL);
    }
    if(CONFIG_NUKA_AUDIO_SPIKE_STAGE==2){
        power_activity();
        esp_err_t error=audio_probe_channels();
        ESP_LOGI("audio-spike","probe complete: %s heap_int=%u stack_free=%u",esp_err_to_name(error),
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),(unsigned)uxTaskGetStackHighWaterMark(NULL));
        vTaskDeleteWithCaps(NULL);
    }
    ESP_LOGI("audio-spike","starting: 5 seconds capture followed by playback per codec and Wi-Fi power mode; stack=%u",STACK);
    int16_t *recording=heap_caps_calloc(SAMPLES+FRAME,sizeof(int16_t),MALLOC_CAP_SPIRAM);
    int16_t *decoded=heap_caps_calloc(SAMPLES+FRAME,sizeof(int16_t),MALLOC_CAP_SPIRAM);
    ESP_ERROR_CHECK(recording&&decoded?ESP_OK:ESP_ERR_NO_MEM);
    wifi_ps_type_t original;ESP_ERROR_CHECK(esp_wifi_get_ps(&original));
    esp_err_t error=ESP_OK;
    unsigned modes=CONFIG_NUKA_AUDIO_SPIKE_STAGE==3?1:3;
    unsigned codecs=CONFIG_NUKA_AUDIO_SPIKE_STAGE==3?1:2;
    for(unsigned mode=0;mode<modes&&error==ESP_OK;++mode){
        power_activity();vTaskDelay(pdMS_TO_TICKS(100));
        ESP_ERROR_CHECK(esp_wifi_set_ps((wifi_ps_type_t)mode));
        for(unsigned compressed=0;compressed<codecs&&error==ESP_OK;++compressed){
            power_activity();
            const char *codec=compressed?"opus":"pcm";int peak=0;
            cpu_sample_t start=cpu_sample();audio_open(true,70);
            size_t minimum=heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            ESP_LOGI("audio-spike","record codec=%s ps=%u",codec,mode);
            error=record(recording,&minimum,&peak);
            int64_t encode_us=0,decode_us=0;size_t payload=0;
            if(compressed&&error==ESP_OK){
                int status;OpusEncoder *encoder=opus_encoder_create(16000,1,OPUS_APPLICATION_VOIP,&status);
                ESP_ERROR_CHECK(status==OPUS_OK&&encoder?ESP_OK:ESP_FAIL);
                OpusDecoder *decoder=opus_decoder_create(16000,1,&status);
                ESP_ERROR_CHECK(status==OPUS_OK&&decoder?ESP_OK:ESP_FAIL);
                ESP_ERROR_CHECK(opus_encoder_ctl(encoder,OPUS_SET_BITRATE(24000))==OPUS_OK?ESP_OK:ESP_FAIL);
                ESP_ERROR_CHECK(opus_encoder_ctl(encoder,OPUS_SET_COMPLEXITY(5))==OPUS_OK?ESP_OK:ESP_FAIL);
                unsigned char packet[512];
                for(unsigned offset=0;offset<SAMPLES&&error==ESP_OK;offset+=FRAME){
                    int64_t now=esp_timer_get_time();int bytes=opus_encode(encoder,recording+offset,FRAME,packet,sizeof(packet));
                    encode_us+=esp_timer_get_time()-now;
                    if(bytes<0){error=ESP_FAIL;break;}payload+=bytes;
                    now=esp_timer_get_time();int count=opus_decode(decoder,packet,bytes,decoded+offset,FRAME,0);
                    decode_us+=esp_timer_get_time()-now;if(count!=FRAME)error=ESP_FAIL;
                    size_t available=heap_caps_get_free_size(MALLOC_CAP_INTERNAL);if(available<minimum)minimum=available;
                    vTaskDelay(1);
                }
                opus_encoder_destroy(encoder);opus_decoder_destroy(decoder);
            }else payload=SAMPLES*sizeof(int16_t);
            if(error==ESP_OK){ESP_LOGI("audio-spike","play codec=%s ps=%u",codec,mode);error=audio_write(compressed?decoded:recording,SAMPLES);}
            metrics(codec,mode,start,minimum,payload,peak,encode_us,decode_us);audio_close();
        }
    }
    ESP_ERROR_CHECK(esp_wifi_set_ps(original));
    free(recording);free(decoded);
    if(error!=ESP_OK)ESP_LOGE("audio-spike","operation: %s",esp_err_to_name(error));
    ESP_LOGI("audio-spike","complete heap_int=%u psram_free=%u stack_free=%u",
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),(unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        (unsigned)uxTaskGetStackHighWaterMark(NULL));
    vTaskDeleteWithCaps(NULL);
}
void audio_spike_start(void)
{ ESP_ERROR_CHECK(xTaskCreatePinnedToCoreWithCaps(spike,"audio_spike",STACK,NULL,4,NULL,1,
    CONFIG_NUKA_AUDIO_SPIKE_STAGE==4?MALLOC_CAP_SPIRAM:MALLOC_CAP_INTERNAL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM); }
#else
void audio_spike_start(void) {}
#endif
