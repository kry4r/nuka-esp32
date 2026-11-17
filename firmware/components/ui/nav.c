// SPDX-License-Identifier: AGPL-3.0-or-later
#include "nav.h"
#include "theme.h"
#include "store.h"
#include "i18n.h"
#include "hub_client.h"
#include "net.h"
#include "battery.h"
#include "overlay.h"
#include "face_view.h"
#include "keynav.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
static lv_obj_t *card, *page, *message, *pager;
static lv_obj_t *status_bar, *status_left, *status_right, *title_group;
static lv_obj_t *back_icon, *title, *offline_chip, *link_icon, *dnd_icon, *charge_icon, *battery_cell, *battery, *battery_text, *clock_text;
static lv_group_t *focus;
static unsigned current;
static bool detail;
static bool arranging;
static unsigned order[11], order_count;
static view_state_t forced_state = VIEW_NORMAL;
static uint32_t last_refresh;
static uint32_t detail_revision;
static bool pointer_down, long_sent, tapped;
static int start_x,start_y;
static uint32_t touch_at,last_tap;
static esp_err_t runtime_error;
void nav_runtime_error(esp_err_t error) { if(error!=ESP_OK)runtime_error=error; }
static void load_order(void)
{
    char saved[sizeof(nuka_settings.cards)];strcpy(saved,nuka_settings.cards);order_count=0;
    char *save;
    for(char *id=strtok_r(saved,",",&save);id;id=strtok_r(NULL,",",&save))
        for(unsigned i=0;i<nuka_app_count;++i)if(!strcmp(id,nuka_apps[i]->id))order[order_count++]=i;
}
static void save_order(void)
{
    nuka_settings.cards[0]=0;
    for(unsigned i=0;i<order_count;++i){if(i)strcat(nuka_settings.cards,",");strcat(nuka_settings.cards,nuka_apps[order[i]]->id);}
    nav_runtime_error(store_save_settings());
}
// Toggling a flag invalidates the object even when nothing changes, so only touch it on a real change.
static void show(lv_obj_t *obj, bool visible) { if(lv_obj_has_flag(obj,LV_OBJ_FLAG_HIDDEN)==visible)lv_obj_set_flag(obj,LV_OBJ_FLAG_HIDDEN,!visible); }
static bool on_clock(void) { return !detail && !strcmp(nav_current()->id,"clock"); }
static void set_title(const char *text)
{
    ui_text_set(title,"%s",text);show(back_icon,detail);
    show(title_group,!on_clock());show(clock_text,!on_clock());
}
static void app_title(void) { char key[40]; snprintf(key, sizeof(key), "app.%s", nav_current()->id); set_title(tr(key)); }
static void update_pager(void)
{
    unsigned index=0;for(unsigned i=0;i<order_count;++i)if(order[i]==current)index=i;
    ui_pager_set(pager,order_count,index);show(pager,!detail&&order_count>1);
}
static void update_status(view_state_t state)
{
    bool wifi=net_connected(),hub=hub_connected();
    bool offline=state==VIEW_OFFLINE||!hub;
    show(offline_chip,offline);show(link_icon,!offline);
    ui_icon_set(link_icon,wifi&&hub?ICON_WIFI:ICON_OFFLINE,wifi&&hub?NUKA_COLOR_TEXT_DIM:NUKA_COLOR_WARN);
    show(dnd_icon,nuka_settings.dnd);
    axp_status_t power=battery_status();
    show(charge_icon,power.vbus_present);show(battery_cell,power.batt_present);
    lv_bar_set_value(battery,power.pct,LV_ANIM_OFF);
    ui_text_set(battery_text,"%u%%",power.pct);
    lv_color_t level=lv_color_hex(power.charging?NUKA_COLOR_AVO:power.pct<10?NUKA_COLOR_DANGER:power.pct<20?NUKA_COLOR_WARN:NUKA_COLOR_TEXT);
    if(!lv_color_eq(lv_obj_get_style_bg_color(battery,LV_PART_INDICATOR),level))lv_obj_set_style_bg_color(battery,level,LV_PART_INDICATOR);
    time_t now=time(NULL);struct tm local;localtime_r(&now,&local);char text[8];strftime(text,sizeof(text),"%H:%M",&local);ui_text_set(clock_text,"%s",text);
    // Keep the title on the physical centre even when the two status groups have different widths.
    lv_obj_update_layout(status_bar);
    int32_t side=LV_MAX(lv_obj_get_width(status_left),lv_obj_get_width(status_right));
    lv_obj_set_style_max_width(title,LV_MIN(220,384-2*(side+8))-(detail?22:0),0);
}
static void open_card(void)
{
    lv_obj_clean(card); detail = false;
    show(card,true);show(page,false);show(message,false);
    app_title();update_pager();
    nav_current()->view->create(card); last_refresh = 0; hub_set_card(nav_current()->id);
}
static void card_clicked(lv_event_t *event) { if(tapped&&nav_current()->view->open_detail)nav_detail(); }
static void title_clicked(lv_event_t *event) { if(detail)nav_back(); }
static void create_status_bar(void)
{
    status_bar=ui_box(lv_screen_active(),384,40);lv_obj_align(status_bar,LV_ALIGN_TOP_MID,0,12);
    status_left=ui_hstack(status_bar,NUKA_SPACE_XS);lv_obj_align(status_left,LV_ALIGN_LEFT_MID,0,0);
    clock_text=ui_text(status_left,22,NUKA_COLOR_TEXT_DIM);
    offline_chip=ui_text(status_left,22,NUKA_COLOR_DANGER);lv_label_set_text(offline_chip,tr("metric.offline"));
    link_icon=ui_icon(status_left,ICON_WIFI,20,NUKA_COLOR_TEXT_DIM);
    title_group=ui_hstack(status_bar,NUKA_SPACE_XS);lv_obj_center(title_group);
    lv_obj_add_flag(title_group,LV_OBJ_FLAG_CLICKABLE);lv_obj_set_ext_click_area(title_group,NUKA_SPACE_M);
    lv_obj_add_event_cb(title_group,title_clicked,LV_EVENT_CLICKED,NULL);
    back_icon=ui_icon(title_group,ICON_BACK,18,NUKA_COLOR_AVO);
    title=ui_text(title_group,22,NUKA_COLOR_AVO);ui_text_clip(title);lv_obj_set_style_max_width(title,220,0);
    lv_obj_t *right=status_right=ui_hstack(status_bar,NUKA_SPACE_XS);lv_obj_align(right,LV_ALIGN_RIGHT_MID,0,0);
    dnd_icon=ui_icon(right,ICON_MOON,18,NUKA_COLOR_TEXT_DIM);
    charge_icon=ui_icon(right,ICON_BOLT,18,NUKA_COLOR_AVO);
    lv_obj_t *cell=battery_cell=ui_hstack(right,1);
    battery=lv_bar_create(cell);lv_obj_set_size(battery,24,14);lv_bar_set_range(battery,0,100);
    lv_obj_set_style_radius(battery,5,0);lv_obj_set_style_bg_opa(battery,LV_OPA_TRANSP,0);
    lv_obj_set_style_border_width(battery,2,0);lv_obj_set_style_border_color(battery,lv_color_hex(NUKA_COLOR_TEXT_DIM),0);lv_obj_set_style_pad_all(battery,2,0);
    lv_obj_set_style_radius(battery,2,LV_PART_INDICATOR);
    lv_obj_t *nub=ui_box(cell,3,6);lv_obj_set_style_bg_opa(nub,LV_OPA_COVER,0);lv_obj_set_style_bg_color(nub,lv_color_hex(NUKA_COLOR_TEXT_DIM),0);lv_obj_set_style_radius(nub,1,0);
    battery_text=ui_text(cell,22,NUKA_COLOR_TEXT_DIM);
}
void nav_init(void)
{
    load_order();
    focus = lv_group_create();
    create_status_bar();
    // Full-screen card whose padding leaves a 432x356 content box: views such as the clock's battery ring may
    // draw out to the bezel, which LVGL would clip to a smaller parent.
    card = ui_box(lv_screen_active(), 480, 480);
    lv_obj_set_style_pad_top(card, 60, 0); lv_obj_set_style_pad_bottom(card, 64, 0); lv_obj_set_style_pad_hor(card, 24, 0);
    lv_obj_add_flag(card,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(card,card_clicked,LV_EVENT_CLICKED,NULL);
    lv_obj_move_to_index(card, 0);  // below the status bar so its taps still land there
    page = lv_obj_create(lv_screen_active()); lv_obj_set_size(page, 432, 408);
    lv_obj_set_flex_flow(page,LV_FLEX_FLOW_COLUMN);lv_obj_set_style_pad_row(page,NUKA_SPACE_S,0);lv_obj_set_style_pad_bottom(page,NUKA_SPACE_XL,0);
    lv_obj_align(page, LV_ALIGN_TOP_MID, 0, 60);
    message = ui_message(lv_screen_active()); lv_obj_align(message, LV_ALIGN_CENTER, 0, 8);
    pager = ui_pager(lv_screen_active()); lv_obj_align(pager, LV_ALIGN_BOTTOM_MID, 0, -22);
    lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN); open_card();
}
void nav_tick(void)
{
    keynav_tick();
    face_view_sync();
    if(runtime_error!=ESP_OK){ESP_LOGE("ui","operation: %s",esp_err_to_name(runtime_error));runtime_error=ESP_OK;}
    // Keep the pressed object alive until LVGL has delivered its release event.
    if (pointer_down) return;
    if (lv_tick_get() - last_refresh < 1000) return;
    last_refresh = lv_tick_get();
    const app_def_t *app = nav_current();
    cJSON *vm = app->topic == VM_NONE ? NULL : vm_store_snapshot(app->topic);
    view_state_t state = forced_state;
    if (state == VIEW_NORMAL && app->topic != VM_NONE && !hub_feature_enabled(vm_topic_name(app->topic))) state = VIEW_UNCONFIGURED;
    if (state == VIEW_NORMAL && app->topic != VM_NONE && !hub_connected() && vm) state = VIEW_OFFLINE;
    if (state == VIEW_NORMAL && app->topic != VM_NONE && !vm) state = VIEW_LOADING;
    update_status(state);
    if (!detail) {
        bool placeholder = state == VIEW_LOADING || state == VIEW_UNCONFIGURED;
        show(card, !placeholder); show(message, placeholder);
        if (placeholder) ui_message_set(message, tr(state == VIEW_LOADING ? "state.loading" : "state.unconfigured"));
        else app->view->apply(vm, state);
    }
    else if (!arranging && (app->topic == VM_HOSTS || app->topic == VM_SERVERS)) {
        cJSON *d = vm_store_snapshot(app->topic == VM_HOSTS ? VM_HOST_DETAIL : VM_SERVER_DETAIL);
        const cJSON *rev = cJSON_GetObjectItemCaseSensitive(d,"rev");
        if (rev && rev->valuedouble != detail_revision) {
            detail_revision = rev->valuedouble;
            int32_t scroll = lv_obj_get_scroll_y(page); lv_obj_clean(page); app->view->open_detail(page);
            lv_obj_update_layout(page); lv_obj_scroll_to_y(page, scroll, LV_ANIM_OFF);
        }
        cJSON_Delete(d);
    }
    cJSON_Delete(vm);
}
static void step_card(int direction)
{
    unsigned selected=0;for(unsigned i=0;i<order_count;++i)if(order[i]==current)selected=i;
    if(!detail)nav_current()->on_action(ACT_BACK);
    nav_back();
    selected=(selected+order_count+direction)%order_count;current=order[selected];open_card();
}
void nav_next(void) { step_card(1); }
void nav_show(const char *id)
{
    for (unsigned i = 0; i < nuka_app_count; ++i) if (strcmp(nuka_apps[i]->id, id) == 0) {
        if (!detail) nav_current()->on_action(ACT_BACK);
        nav_back(); current = i; open_card(); return;
    }
}
void nav_home(void) { nav_show("clock"); }
static void enter_page(void)
{
    detail = true; lv_obj_clean(page); lv_group_remove_all_objs(focus);
    // LVGL only auto-focuses the first group member if it is visible, so show the page before filling it.
    show(card,false); show(message,false); show(page,true); update_pager();
}
void nav_detail(void)
{
    const app_def_t *app = nav_current();
    if (!app->view->open_detail) return;
    app->on_action(ACT_OPEN); arranging=false; detail_revision = 0; enter_page(); app_title();
    app->view->open_detail(page);
}
void nav_back(void)
{
    if (detail&&!arranging) nav_current()->on_action(ACT_BACK);
    detail = false;arranging=false; show(page,false); show(card,true);
    app_title(); update_pager(); last_refresh = 0;
}
bool nav_in_detail(void) { return detail; }
bool nav_arranging(void) { return arranging; }
const app_def_t *nav_current(void) { return nuka_apps[current]; }
lv_group_t *nav_focus_group(void) { return focus; }
static void focus_children(lv_obj_t *parent)
{
    for(unsigned i=0;i<lv_obj_get_child_count(parent);++i){lv_obj_t *object=lv_obj_get_child(parent,i);
        if(lv_obj_check_type(object,&lv_button_class)||lv_obj_check_type(object,&lv_slider_class)||lv_obj_check_type(object,&lv_dropdown_class)||lv_obj_check_type(object,&lv_spinbox_class)||lv_obj_check_type(object,&lv_checkbox_class))lv_group_add_obj(focus,object);
        focus_children(object);
    }
}
void nav_restore_focus(void) { lv_group_remove_all_objs(focus);if(detail)focus_children(page); }
void nav_set_state(view_state_t state) { forced_state = state; last_refresh = 0; }
void nav_refresh(void) { last_refresh=lv_tick_get()-1000; }
void nav_reload(void) { bool was_detail=detail;open_card();if(was_detail)nav_detail(); }
static bool pressing_control(void)
{
    lv_obj_t *object=lv_indev_get_active_obj();
    return object&&object!=card&&lv_obj_has_flag(object,LV_OBJ_FLAG_CLICKABLE);
}
void nav_touch(bool pressed,int x,int y)
{
    uint32_t now=lv_tick_get();
    if(pressed&&!pointer_down){start_x=x;start_y=y;touch_at=now;long_sent=false;tapped=false;}
    // Long press on empty card space arranges cards; on a control it belongs to that control.
    if(pressed&&pointer_down&&!detail&&!long_sent&&overlay_current()==OVERLAY_NONE&&abs(x-start_x)<12&&abs(y-start_y)<12&&now-touch_at>=600&&!pressing_control()){long_sent=true;lv_indev_wait_release(lv_indev_active());nav_arrange();}
    if(!pressed&&pointer_down&&overlay_current()==OVERLAY_NONE){
        int dx=x-start_x,dy=y-start_y;
        tapped=abs(dx)<12&&abs(dy)<12&&now-touch_at<400;
        if(start_y<48&&dy>60)overlay_quick();
        else if(start_y>432&&dy<-60)overlay_notifications();
        else if(detail&&start_x<48&&dx>80)nav_back();
        else if(!detail&&abs(dx)>40&&abs(dx)>abs(dy))step_card(dx<0?1:-1);
        else if(tapped&&!detail&&!strcmp(nav_current()->id,"clock")){
            face_view_touch(x,y);
            if(now-last_tap<300)nav_current()->on_action(ACT_SECONDARY);
            last_tap=now;
        }
    }
    pointer_down=pressed;
}
static void rearrange(void *scroll)
{
    if(!arranging)return;
    nav_arrange();lv_obj_update_layout(page);lv_obj_scroll_to_y(page,(intptr_t)scroll,LV_ANIM_OFF);
}
static void edit_order(lv_event_t *event)
{
    unsigned value=(uintptr_t)lv_event_get_user_data(event), index=value/3, action=value%3;
    if(action==0){unsigned selected=0;while(selected<order_count&&order[selected]!=index)++selected;
        if(selected==order_count)order[order_count++]=index;
        else if(order_count>1){memmove(order+selected,order+selected+1,(order_count-selected-1)*sizeof(*order));--order_count;}
    }else{unsigned selected=0;while(selected<order_count&&order[selected]!=index)++selected;
        if(selected<order_count&&((action==1&&selected>0)||(action==2&&selected+1<order_count))){unsigned target=action==1?selected-1:selected+1,old=order[selected];order[selected]=order[target];order[target]=old;}
    }
    vm_topic_t topic=nuka_apps[index]->topic;if(topic!=VM_NONE){bool visible=false;for(unsigned i=0;i<order_count;++i)visible|=order[i]==index;hub_set_feature(vm_topic_name(topic),visible);}
    save_order();
    // The pressed button lives in the page, so rebuild after its event has finished.
    lv_async_call(rearrange,(void *)(intptr_t)lv_obj_get_scroll_y(page));
}
static void arrange_row(unsigned index,bool visible,bool first,bool last)
{
    char key[40];snprintf(key,sizeof(key),"app.%s",nuka_apps[index]->id);
    lv_obj_t *row=ui_box(page,LV_PCT(100),72);
    lv_obj_set_flex_flow(row,LV_FLEX_FLOW_ROW);lv_obj_set_flex_align(row,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row,NUKA_SPACE_S,0);
    lv_obj_t *label=ui_text(row,28,visible?NUKA_COLOR_TEXT:NUKA_COLOR_TEXT_DIM);lv_label_set_text(label,tr(key));
    ui_text_clip(label);lv_obj_set_flex_grow(label,1);
    if(visible){
        lv_obj_t *up=ui_icon_button(row,ICON_UP,56,false,edit_order,(void *)(uintptr_t)(index*3+1));
        lv_obj_t *down=ui_icon_button(row,ICON_DOWN,56,false,edit_order,(void *)(uintptr_t)(index*3+2));
        if(first)lv_obj_add_state(up,LV_STATE_DISABLED);
        if(last)lv_obj_add_state(down,LV_STATE_DISABLED);
        lv_group_add_obj(focus,up);lv_group_add_obj(focus,down);
    }
    lv_obj_t *toggle=ui_icon_button(row,ICON_CHECK,56,visible,edit_order,(void *)(uintptr_t)(index*3));
    if(!visible)ui_icon_set(lv_obj_get_child(toggle,0),ICON_CHECK,NUKA_COLOR_OUTLINE);
    lv_group_add_obj(focus,toggle);
}
void nav_arrange(void)
{
    arranging=true;enter_page();set_title(tr("settings.cards"));
    for(unsigned position=0;position<order_count;++position)arrange_row(order[position],true,position==0,position+1==order_count);
    for(unsigned candidate=0;candidate<nuka_app_count;++candidate){
        bool visible=false;for(unsigned j=0;j<order_count;++j)visible|=order[j]==candidate;
        if(!visible)arrange_row(candidate,false,false,false);
    }
}
