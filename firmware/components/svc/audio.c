// SPDX-License-Identifier: AGPL-3.0-or-later
#include "audio.h"
#include "axp2101.h"
#include "i2c_bus.h"
#include "pins.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdlib.h>

enum { RATE = 16000, BLOCK = 160 };
typedef struct { audio_codec_ctrl_if_t base; uint8_t address; } codec_control_t;
static i2s_chan_handle_t tx, rx;
static const audio_codec_if_t *dac, *adc;
static bool playing;
static bool audible;
#ifdef CONFIG_NUKA_AUDIO_SPIKE
static void diagnostic_registers(void)
{
    uint8_t enabled,voltage;
    ESP_ERROR_CHECK(i2c_bus_rd(0x34,0x90,&enabled,1));
    ESP_ERROR_CHECK(i2c_bus_rd(0x34,0x92,&voltage,1));
    ESP_LOGI("audio-spike","ALDO1 enabled=%u voltage_code=%u amp=%d",enabled&1,voltage,gpio_get_level(PIN_AUDIO_EN));
    const uint8_t registers[]={0x00,0x01,0x02,0x06,0x09,0x0d,0x0e,0x12,0x14,0x31,0x32};
    for(unsigned i=0;i<sizeof(registers);++i){
        int value;ESP_ERROR_CHECK(dac->get_reg(dac,registers[i],&value));
        ESP_LOGI("audio-spike","ES8311 reg=%02x value=%02x",registers[i],value);
    }
}
#endif

static bool control_open(const audio_codec_ctrl_if_t *control) { return true; }
static int control_read(const audio_codec_ctrl_if_t *control, int reg, int reg_len, void *data, int size)
{ return i2c_bus_rd(((const codec_control_t *)control)->address, reg, data, size); }
static int control_write(const audio_codec_ctrl_if_t *control, int reg, int reg_len, void *data, int size)
{ return i2c_bus_wr(((const codec_control_t *)control)->address, reg, data, size); }
static const codec_control_t dac_control = {
    .base = {.is_open=control_open,.read_reg=control_read,.write_reg=control_write}, .address=0x18
};
static const codec_control_t adc_control = {
    .base = {.is_open=control_open,.read_reg=control_read,.write_reg=control_write}, .address=0x40
};

static void open_format(bool microphone, unsigned volume, unsigned bits)
{
    audible=volume>0;
    gpio_config_t amp = {.pin_bit_mask=1ULL<<PIN_AUDIO_EN,.mode=GPIO_MODE_OUTPUT};
    ESP_ERROR_CHECK(gpio_config(&amp));
    ESP_ERROR_CHECK(gpio_set_level(PIN_AUDIO_EN,0));
    ESP_ERROR_CHECK(axp_set_aldo1(true));
    vTaskDelay(pdMS_TO_TICKS(20));
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,I2S_ROLE_MASTER);
    channel.dma_desc_num=3;channel.dma_frame_num=BLOCK;channel.auto_clear=true;
    ESP_ERROR_CHECK(i2s_new_channel(&channel,&tx,microphone?&rx:NULL));
    i2s_std_config_t format = {
        .clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(RATE),
        .slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bits,I2S_SLOT_MODE_STEREO),
        .gpio_cfg={.mclk=PIN_AUDIO_MCLK,.bclk=PIN_AUDIO_BCLK,.ws=PIN_AUDIO_WS,
            .dout=PIN_AUDIO_DOUT,.din=microphone?PIN_AUDIO_DIN:I2S_GPIO_UNUSED}
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx,&format));
    if(microphone)ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx,&format));
    ESP_ERROR_CHECK(i2s_channel_enable(tx));
    if(microphone)ESP_ERROR_CHECK(i2s_channel_enable(rx));
    es8311_codec_cfg_t speaker = {.ctrl_if=&dac_control.base,.codec_mode=ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin=-1,.use_mclk=true,.no_dac_ref=true,.mclk_div=256};
    dac=es8311_codec_new(&speaker);ESP_ERROR_CHECK(dac?ESP_OK:ESP_ERR_NO_MEM);
    esp_codec_dev_sample_info_t samples = {.bits_per_sample=bits,.channel=2,.sample_rate=RATE,.mclk_multiple=256};
    ESP_ERROR_CHECK(dac->set_fs(dac,&samples));
    ESP_ERROR_CHECK(dac->enable(dac,true));
    ESP_ERROR_CHECK(dac->set_vol(dac,volume?(-50.0f+volume*0.5f):-95.5f));
    ESP_ERROR_CHECK(dac->mute(dac,true));
    if(microphone){
        es7210_codec_cfg_t microphones = {.ctrl_if=&adc_control.base,
            .mic_selected=ES7210_SEL_MIC1|ES7210_SEL_MIC2,.mclk_div=256};
        adc=es7210_codec_new(&microphones);ESP_ERROR_CHECK(adc?ESP_OK:ESP_ERR_NO_MEM);
        ESP_ERROR_CHECK(adc->set_fs(adc,&samples));
        ESP_ERROR_CHECK(adc->enable(adc,true));
        ESP_ERROR_CHECK(adc->set_mic_gain(adc,24));
        ESP_ERROR_CHECK(adc->mute_mic(adc,false));
    }
}

void audio_open(bool microphone, unsigned volume) { open_format(microphone,volume,16); }

esp_err_t audio_probe_channels(void)
{
    esp_err_t error=ESP_OK;
    for(unsigned bits=16;bits<=32&&error==ESP_OK;bits+=16){
        open_format(true,0,bits);
        int32_t buffer[BLOCK*2];size_t received;
        for(unsigned i=0;i<100&&error==ESP_OK;++i)
            error=i2s_channel_read(rx,buffer,BLOCK*2*(bits/8),&received,1000);
        int peak[2]={0};int64_t sum[2]={0};uint64_t squares[2]={0};uint32_t bit_or[2]={0};
        ESP_LOGI("audio-spike","probe capture begin: bits=%u channels=2 duration=5s gain=24dB",bits);
        for(unsigned frame=0;frame<500&&error==ESP_OK;++frame){
            error=i2s_channel_read(rx,buffer,BLOCK*2*(bits/8),&received,1000);
            if(error!=ESP_OK)break;
            if(received!=BLOCK*2*(bits/8)){error=ESP_ERR_INVALID_SIZE;break;}
            for(unsigned i=0;i<BLOCK*2;++i){
                unsigned channel=i%2;
                int value=bits==16?((int16_t *)buffer)[i]:buffer[i]>>16;
                int magnitude=abs(value);if(magnitude>peak[channel])peak[channel]=magnitude;
                sum[channel]+=value;squares[channel]+=(int64_t)value*value;
                bit_or[channel]|=bits==16?(uint16_t)value:(uint32_t)buffer[i];
            }
        }
        int format,gain1,gain2;
        ESP_ERROR_CHECK(adc->get_reg(adc,0x11,&format));
        ESP_ERROR_CHECK(adc->get_reg(adc,0x43,&gain1));
        ESP_ERROR_CHECK(adc->get_reg(adc,0x44,&gain2));
        ESP_LOGI("audio-spike","ES7210 bits=%u format=%02x gain1=%02x gain2=%02x",bits,format,gain1,gain2);
        for(unsigned channel=0;channel<2;++channel)
            ESP_LOGI("audio-spike","probe bits=%u ch=%u peak=%d rms=%.2f dc=%.2f raw_or=%08lx",bits,channel,
                peak[channel],sqrt((double)squares[channel]/80000),sum[channel]/80000.0,(unsigned long)bit_or[channel]);
        audio_close();
    }
    return error;
}

esp_err_t audio_read(int16_t *mono, size_t count)
{
    int16_t stereo[BLOCK*2];
    while(count){
        size_t block=count<BLOCK?count:BLOCK, received;
        esp_err_t error=i2s_channel_read(rx,stereo,block*4,&received,1000);
        if(error!=ESP_OK)return error;
        if(received!=block*4)return ESP_ERR_INVALID_SIZE;
        for(size_t i=0;i<block;++i)mono[i]=stereo[i*2];
        mono+=block;count-=block;
    }
    return ESP_OK;
}

esp_err_t audio_write(const int16_t *mono, size_t count)
{
    if(!playing){
        int16_t silence[BLOCK*2]={0};size_t sent;
        esp_err_t error=i2s_channel_write(tx,silence,sizeof(silence),&sent,1000);
        if(error==ESP_OK&&audible)error=gpio_set_level(PIN_AUDIO_EN,1);
        if(error==ESP_OK&&audible)error=dac->mute(dac,false);
        if(error!=ESP_OK)return error;
        playing=true;
#ifdef CONFIG_NUKA_AUDIO_SPIKE
        diagnostic_registers();
#endif
    }
    int16_t stereo[BLOCK*2];
    while(count){
        size_t block=count<BLOCK?count:BLOCK,sent;
        for(size_t i=0;i<block;++i)stereo[i*2]=stereo[i*2+1]=mono[i];
        esp_err_t error=i2s_channel_write(tx,stereo,block*4,&sent,1000);
        if(error!=ESP_OK)return error;
        if(sent!=block*4)return ESP_ERR_INVALID_SIZE;
        mono+=block;count-=block;
    }
    return ESP_OK;
}

void audio_close(void)
{
    if(playing)vTaskDelay(pdMS_TO_TICKS(40));
    ESP_ERROR_CHECK(gpio_set_level(PIN_AUDIO_EN,0));
    ESP_ERROR_CHECK(dac->mute(dac,true));
    ESP_ERROR_CHECK(audio_codec_delete_codec_if(dac));dac=NULL;
    if(adc){ESP_ERROR_CHECK(audio_codec_delete_codec_if(adc));adc=NULL;}
    if(rx){ESP_ERROR_CHECK(i2s_channel_disable(rx));ESP_ERROR_CHECK(i2s_del_channel(rx));rx=NULL;}
    ESP_ERROR_CHECK(i2s_channel_disable(tx));ESP_ERROR_CHECK(i2s_del_channel(tx));tx=NULL;
    ESP_ERROR_CHECK(axp_set_aldo1(false));playing=false;
}

esp_err_t audio_tone(void)
{
    audio_open(false,70);
    static const int16_t wave[]={0,627,1159,1514,1638,1514,1159,627,0,-627,-1159,-1514,-1638,-1514,-1159,-627};
    int16_t block[BLOCK];
    for(unsigned i=0;i<BLOCK;++i)block[i]=wave[i%16];
    esp_err_t error=ESP_OK;
    for(unsigned i=0;i<50&&error==ESP_OK;++i)error=audio_write(block,BLOCK);
    audio_close();return error;
}
