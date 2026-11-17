// SPDX-License-Identifier: AGPL-3.0-or-later
#include "face_view.h"
#include "app_helpers.h"
#include "anim.h"
#include "home_app.h"
#include "pet_app.h"
#include "battery.h"
#include "hub_client.h"
#include "power.h"
#include "overlay.h"
#include "esp_random.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef enum { FACE_IDLE, FACE_ATTENTION, FACE_LISTENING, FACE_THINKING, FACE_SPEAKING,
    FACE_HAPPY, FACE_BUSY, FACE_WORRIED, FACE_HUNGRY, FACE_SLEEPY, FACE_CHARGING,
    FACE_DIZZY, FACE_OFFLINE, FACE_ERROR } face_state_t;
typedef struct { float width,height,radius,y; uint32_t color; } pose_t;
static const pose_t poses[] = {
    [FACE_IDLE]={86,124,40,0,NUKA_COLOR_AVO}, [FACE_ATTENTION]={94,144,40,-4,NUKA_COLOR_AVO_SOFT},
    [FACE_LISTENING]={94,136,42,-2,NUKA_COLOR_AVO_SOFT}, [FACE_THINKING]={78,106,36,-12,NUKA_COLOR_AVO},
    [FACE_SPEAKING]={88,126,40,0,NUKA_COLOR_AVO}, [FACE_HAPPY]={96,72,42,-8,NUKA_COLOR_AVO_SOFT},
    [FACE_BUSY]={82,62,26,12,NUKA_COLOR_AVO}, [FACE_WORRIED]={84,108,32,4,NUKA_COLOR_WARN},
    [FACE_HUNGRY]={76,72,30,20,NUKA_COLOR_WARN}, [FACE_SLEEPY]={88,34,18,28,NUKA_COLOR_AVO_DEEP},
    [FACE_CHARGING]={86,12,6,16,NUKA_COLOR_AVO}, [FACE_DIZZY]={88,112,40,0,NUKA_COLOR_AVO},
    [FACE_OFFLINE]={86,12,6,16,NUKA_COLOR_TEXT_DIM}, [FACE_ERROR]={88,60,24,8,NUKA_COLOR_DANGER},
};
static lv_obj_t *root, *glow[2], *eye[2], *lid[2], *spiral[2], *bolt, *caption;
static lv_timer_t *timer;
static face_state_t state;
static pose_t from, current;
static uint32_t changed, blink_at, blink_started, look_at, touched_until, hop_at, hop_started;
static int look_x, look_y;
static float gaze_x,gaze_y;
static bool visible,timer_running;
static uint32_t renders;
#ifdef NUKA_SIM
static int preview_state=-1;
bool face_view_preview(const char *name)
{
    static const char *names[]={"idle","attention","listening","thinking","speaking","happy","busy","worried","hungry","sleepy","charging","dizzy","offline","error"};
    for(unsigned i=0;i<sizeof(names)/sizeof(*names);++i)if(!strcmp(name,names[i])){preview_state=i;return true;}
    return false;
}
void face_view_preview_stats(unsigned *frames,bool *active) { *frames=renders;*active=timer_running; }
#endif
static const lv_point_precise_t spiral_points[] = {
    {46,46},{49,43},{54,45},{57,51},{54,58},{46,62},{36,59},{30,50},{31,39},
    {40,29},{54,26},{68,33},{77,46},{77,62},{67,77},{49,84},{29,78},{16,61},{15,38},{30,17},{53,9},{79,20},
};
static void deleted(lv_event_t *event)
{
    (void)event;lv_timer_delete(timer);timer=NULL;root=NULL;visible=timer_running=false;
}
void face_view_touch(int x,int y)
{
    if(!visible)return;
    look_x=LV_CLAMP(-14,(x-240)/12,14);look_y=LV_CLAMP(-10,(y-230)/16,10);
    touched_until=lv_tick_get()+1500;blink_started=lv_tick_get();blink_at=blink_started+3000;
    lv_timer_set_period(timer,16);lv_timer_ready(timer);
}
static void set_state(face_state_t next)
{
    if(next==state)return;
    from=current;state=next;changed=lv_tick_get();
    if(next==FACE_ERROR)blink_started=changed;
}
static face_state_t choose_state(void)
{
#ifdef NUKA_SIM
    if(preview_state>=0)return (face_state_t)preview_state;
#endif
    home_voice_state_t voice=home_voice_state();
    if(voice==HOME_VOICE_ERROR)return FACE_ERROR;
    if(voice==HOME_VOICE_LISTENING||voice==HOME_VOICE_PREPARING)return FACE_LISTENING;
    if(voice==HOME_VOICE_THINKING)return FACE_THINKING;
    if(voice==HOME_VOICE_SPEAKING)return FACE_SPEAKING;
    if(overlay_permission_pending())return FACE_ATTENTION;
    if(!hub_connected())return FACE_OFFLINE;
    cJSON *pet=vm_store_snapshot(VM_PET);const char *mood=pet_mood(pet);face_state_t result=FACE_IDLE;
    if(!strcmp(mood,"attention"))result=FACE_ATTENTION;
    else if(!strcmp(mood,"hungry"))result=FACE_WORRIED;
    else if(!strcmp(mood,"dizzy"))result=FACE_DIZZY;
    else if(!strcmp(mood,"celebrate"))result=FACE_HAPPY;
    else if(!strcmp(mood,"busy"))result=FACE_BUSY;
    else if(!strcmp(mood,"charging"))result=FACE_CHARGING;
    else if(!strcmp(mood,"sleep"))result=FACE_SLEEPY;
    cJSON_Delete(pet);
    cJSON *ai=vm_store_snapshot(VM_AI);const cJSON *account,*window;
    cJSON_ArrayForEach(account,cJSON_GetObjectItemCaseSensitive(ai,"accounts")) {
        if(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(account,"breaker_open"))||!strcmp(app_str(account,"status"),"exhausted")) {
            if(result!=FACE_ATTENTION)result=FACE_HUNGRY;
        }
        cJSON_ArrayForEach(window,cJSON_GetObjectItemCaseSensitive(account,"windows")) {
            double used=cJSON_GetObjectItemCaseSensitive(window,"used_pct")->valuedouble;
            if(used>=100&&result!=FACE_ATTENTION)result=FACE_HUNGRY;
            else if(used>90&&result==FACE_IDLE)result=FACE_WORRIED;
        }
    }
    cJSON_Delete(ai);
    cJSON *notify=vm_store_snapshot(VM_NOTIFY);const cJSON *item;
    cJSON_ArrayForEach(item,cJSON_GetObjectItemCaseSensitive(notify,"items"))
        if(!strcmp(app_str(item,"level"),"critical")&&result!=FACE_ATTENTION&&result!=FACE_HUNGRY&&result!=FACE_DIZZY)result=FACE_WORRIED;
    cJSON_Delete(notify);
    if(result==FACE_IDLE) {
        time_t now=time(NULL);struct tm local;localtime_r(&now,&local);
        if(local.tm_hour<7||local.tm_hour>=23)result=FACE_SLEEPY;
    }
    if(touched_until>lv_tick_get()&&result==FACE_IDLE)result=FACE_HAPPY;
    return result;
}
static void render(lv_timer_t *source)
{
    (void)source;
    uint32_t now=lv_tick_get(),elapsed=now-changed;
    float fraction=LV_MIN(1.0f,elapsed/(float)ANIM_BASE),ease=1-powf(1-fraction,3);
    const pose_t *target=&poses[state];
    current=(pose_t){from.width+(target->width-from.width)*ease,from.height+(target->height-from.height)*ease,
        from.radius+(target->radius-from.radius)*ease,from.y+(target->y-from.y)*ease,target->color};
    if(now>=blink_at) { blink_started=now;blink_at=now+3000+esp_random()%4001; }
    unsigned duration=state==FACE_SLEEPY?ANIM_SLOW*2:ANIM_BASE;
    float blink=1;
    if(blink_started&&now-blink_started<duration)blink=fabsf((now-blink_started)/(duration/2.0f)-1);
    if(state==FACE_ERROR&&elapsed<ANIM_SLOW*3) {
        unsigned phase=elapsed%(ANIM_SLOW+ANIM_FAST);
        if(phase<ANIM_BASE)blink=fabsf(phase/(ANIM_BASE/2.0f)-1);
    }
    if(now>=look_at&&now>=touched_until) { look_x=(int)(esp_random()%17)-8;look_y=(int)(esp_random()%9)-4;look_at=now+1800+esp_random()%1801; }
    bool animate=battery_status().pct>=10;
    float phase=animate?now/1000.0f:0;
    gaze_x+=(look_x-gaze_x)*0.18f;gaze_y+=(look_y-gaze_y)*0.18f;
    int x=(int)gaze_x,y=(int)gaze_y;
    if(state==FACE_THINKING) { x=(int)(sinf(phase*2)*15);y=-12; }
    if(state==FACE_ATTENTION)x=10;
    if(state==FACE_HUNGRY) { if(now%7000<1700)x=14;y+=6; }
    if(state==FACE_HAPPY)y-=(int)(fabsf(sinf(elapsed/(float)ANIM_BASE*3.14159f))*12)*(elapsed<ANIM_BASE*2);
    if(state==FACE_IDLE){
        y+=(int)(sinf(phase)*2);
        if(now>=hop_at){hop_started=now;hop_at=now+7000+esp_random()%5001;}
        if(hop_started&&now-hop_started<ANIM_SLOW*2)y-=(int)(sinf((now-hop_started)/(float)(ANIM_SLOW*2)*3.14159f)*6);
    }
    unsigned level=home_voice_level();
#ifdef NUKA_SIM
    if(preview_state==FACE_LISTENING||preview_state==FACE_SPEAKING)level=(unsigned)(50+sinf(phase*8)*35);
#endif
    float envelope=(state==FACE_LISTENING||state==FACE_SPEAKING)?level/100.0f:0;
    if(!animate){blink=1;x=y=0;envelope=0;current=*target;}
    float pulse=state==FACE_ATTENTION?sinf(phase*2)*3:0;
    int height=LV_MAX(6,(int)((current.height+envelope*12+pulse)*blink));
    for(unsigned i=0;i<2;++i) {
        int width=(int)current.width;
        int center_x=128+i*176+x,center_y=165+(int)current.y+y;
        app_show(spiral[i],state==FACE_DIZZY);app_show(glow[i],state!=FACE_DIZZY);
        if(state==FACE_DIZZY) { lv_obj_set_pos(spiral[i],center_x-48,center_y-48);lv_obj_set_style_transform_rotation(spiral[i],animate?(elapsed*3600/1500)%3600:0,0);continue; }
        lv_obj_set_size(glow[i],width+12,height+12);lv_obj_set_pos(glow[i],center_x-(width+12)/2,center_y-(height+12)/2);
        lv_obj_set_style_radius(glow[i],(int)current.radius+6,0);
        lv_obj_set_size(eye[i],width,height);lv_obj_center(eye[i]);lv_obj_set_style_radius(eye[i],(int)current.radius,0);
        lv_obj_set_style_bg_color(eye[i],lv_color_hex(current.color),0);
        bool masked=state==FACE_HAPPY||state==FACE_WORRIED;
        app_show(lid[i],masked);
        if(masked) {
            lv_obj_set_size(lid[i],width+12,state==FACE_HAPPY?height:30);
            lv_obj_set_pos(lid[i],0,state==FACE_HAPPY?height/2:-(i?12:2));
            lv_obj_set_style_radius(lid[i],state==FACE_HAPPY?width/2:6,0);
            lv_obj_set_style_transform_rotation(lid[i],state==FACE_WORRIED?(i?-120:120):0,0);
        }
    }
    app_show(bolt,state==FACE_CHARGING);
    const char *text=home_voice_caption();ui_text_set(caption,"%s",text);app_show(caption,*text);
    lv_obj_set_style_text_color(caption,lv_color_hex(state==FACE_ERROR?NUKA_COLOR_WARN:NUKA_COLOR_TEXT_DIM),0);
    ++renders;
    bool active=elapsed<ANIM_BASE||blink<1||state==FACE_LISTENING||state==FACE_SPEAKING||state==FACE_THINKING||state==FACE_HAPPY||state==FACE_DIZZY;
    lv_timer_set_period(timer,active?16:100);
}
void face_view_create(lv_obj_t *card)
{
    root=app_root(card);lv_obj_set_layout(root,LV_LAYOUT_NONE);
    lv_obj_add_event_cb(root,deleted,LV_EVENT_DELETE,NULL);
    for(unsigned i=0;i<2;++i) {
        glow[i]=ui_box(root,100,140);lv_obj_set_style_bg_opa(glow[i],LV_OPA_COVER,0);lv_obj_set_style_bg_color(glow[i],lv_color_hex(NUKA_COLOR_SURFACE_HI),0);
        eye[i]=ui_box(glow[i],86,124);lv_obj_set_style_bg_opa(eye[i],LV_OPA_COVER,0);
        lid[i]=ui_box(eye[i],100,30);lv_obj_set_style_bg_opa(lid[i],LV_OPA_COVER,0);lv_obj_set_style_bg_color(lid[i],lv_color_hex(NUKA_COLOR_BG),0);
        spiral[i]=lv_line_create(root);lv_line_set_points(spiral[i],spiral_points,sizeof(spiral_points)/sizeof(*spiral_points));
        lv_obj_set_style_line_width(spiral[i],7,0);lv_obj_set_style_line_rounded(spiral[i],true,0);lv_obj_set_style_line_color(spiral[i],lv_color_hex(NUKA_COLOR_AVO),0);
        lv_obj_set_style_transform_pivot_x(spiral[i],46,0);lv_obj_set_style_transform_pivot_y(spiral[i],46,0);
    }
    bolt=ui_icon(root,ICON_BOLT,26,NUKA_COLOR_AVO_SOFT);lv_obj_align(bolt,LV_ALIGN_CENTER,0,0);
    caption=ui_text(root,22,NUKA_COLOR_TEXT_DIM);lv_obj_set_size(caption,360,60);lv_obj_align(caption,LV_ALIGN_BOTTOM_MID,0,-4);
    lv_label_set_long_mode(caption,LV_LABEL_LONG_DOT);lv_obj_set_style_text_align(caption,LV_TEXT_ALIGN_CENTER,0);
    state=FACE_IDLE;current=from=poses[FACE_IDLE];changed=lv_tick_get();blink_started=0;
    blink_at=changed+3000+esp_random()%4001;look_at=changed;hop_at=changed+7000;hop_started=0;gaze_x=gaze_y=0;renders=0;visible=false;
    timer=lv_timer_create(render,100,NULL);lv_timer_pause(timer);timer_running=false;app_show(root,false);
}
void face_view_visible(bool show)
{
    visible=show;app_show(root,show);face_view_sync();
}
void face_view_apply(void)
{
    set_state(choose_state());if(power_screen_on())render(timer);
}
void face_view_sync(void)
{
    if(!root)return;
    bool running=visible&&!nav_in_detail()&&!strcmp(nav_current()->id,"clock")&&power_screen_on()&&battery_status().pct>=10;
    if(running!=timer_running){if(running)lv_timer_resume(timer);else lv_timer_pause(timer);timer_running=running;}
}
