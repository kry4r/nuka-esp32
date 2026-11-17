// SPDX-License-Identifier: AGPL-3.0-or-later
#include "perf_hud.h"
#include "gfx.h"
#include "lvgl.h"
#include "nukapack.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static lv_obj_t *label;
static uint32_t last,previous_frames,previous_total,previous_idle;
void perf_hud_init(void)
{
    label=lv_label_create(lv_layer_top());lv_obj_set_style_text_font(label,nuka_font(22,false),0);lv_obj_set_style_text_color(label,lv_color_white(),0);lv_obj_align(label,LV_ALIGN_TOP_RIGHT,0,0);lv_obj_add_flag(label,LV_OBJ_FLAG_HIDDEN);
}
void perf_hud_tick(void)
{
    uint32_t now=lv_tick_get();if(now-last<1000)return;
    UBaseType_t count=uxTaskGetNumberOfTasks();TaskStatus_t *tasks=malloc(count*sizeof(*tasks));configRUN_TIME_COUNTER_TYPE total;count=uxTaskGetSystemState(tasks,count,&total);uint32_t idle=0;
    for(unsigned i=0;i<count;++i)if(!strncmp(tasks[i].pcTaskName,"IDLE",4))idle+=tasks[i].ulRunTimeCounter;
    free(tasks);uint32_t elapsed=(uint32_t)total-previous_total,available=2*elapsed,cpu=available?100-((idle-previous_idle)*100ULL/available):0;
    uint32_t frames=gfx_frame_count();char text[100];snprintf(text,sizeof(text),"FPS %lu | CPU %lu%% | RAM %u",(unsigned long)(frames-previous_frames)*1000/(now-last),(unsigned long)cpu,(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    lv_label_set_text(label,text);previous_frames=frames;previous_idle=idle;previous_total=total;last=now;
}
void perf_hud_show(bool show){if(show)lv_obj_remove_flag(label,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(label,LV_OBJ_FLAG_HIDDEN);}
