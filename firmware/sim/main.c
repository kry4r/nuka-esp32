// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sim.h"
#include "SDL.h"
#include "lvgl.h"
#include "theme.h"
#include "nav.h"
#include "overlay.h"
#include "keynav.h"
#include "store.h"
#include "i18n.h"
#include "nukapack.h"
#include "power.h"
#include "vault.h"
#include "vault_app.h"
#include "pomodoro_app.h"
#include "home_app.h"
#include "face_view.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>

static SDL_Renderer *renderer;
static SDL_Texture *texture;
static lv_indev_t *pointer_device;
static uint32_t pixels[480 * 480], draw_buffer[480 * 48];
static bool mouse_down, drop_mouse;
static int mouse_x, mouse_y;
static struct { bool down, consumed, waking; uint32_t at; } keys[KEY_COUNT];
static uint32_t key3_pending;
static bool recovery;
static void overlay_dismissed(lv_event_t *event) { (void)event; overlay_close(); }

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *bytes)
{
    size_t width = area->x2 - area->x1 + 1;
    for (int y = area->y1; y <= area->y2; ++y)
        memcpy(pixels + y * 480 + area->x1, bytes + (y - area->y1) * width * 4, width * 4);
    lv_display_flush_ready(display);
}
static void pointer_read(lv_indev_t *pointer, lv_indev_data_t *data)
{
    (void)pointer;
    data->state = mouse_down && !drop_mouse && power_screen_on() ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->point.x = mouse_x; data->point.y = mouse_y;
    nav_touch(data->state == LV_INDEV_STATE_PRESSED, mouse_x, mouse_y);
}
static void emit(key_id_t key, key_event_kind_t kind, uint16_t held)
{ key_event_t event = {key, kind, held}; keynav_handle(&event); }
static void key_event(key_id_t key, bool down)
{
    uint32_t now = SDL_GetTicks();
    if (down) {
        if (keys[key].down) return;
        keys[key].down = true; keys[key].at = now; keys[key].consumed = keys[key].waking = false;
        if (!power_screen_on()) { power_activity(); keys[key].waking = true; keys[key].consumed = key != KEY_PWR; }
        emit(key, KEYEV_PRESS, 0);
        return;
    }
    if (!keys[key].down) return;
    keys[key].down = false;
    uint32_t held = now - keys[key].at;
    emit(key, KEYEV_RELEASE, held);
    if (keys[key].consumed || keys[key].waking) return;
    if (held >= (key == KEY_KEY3 ? 400 : 600)) return;
    if (key == KEY_KEY3) {
        if (key3_pending && now - key3_pending < 300) { key3_pending = 0; emit(key, KEYEV_DOUBLE, held); }
        else key3_pending = now;
    } else emit(key, KEYEV_SHORT, held);
}
static void key_tick(void)
{
    uint32_t now = SDL_GetTicks();
    if (key3_pending && now - key3_pending >= 300) { key3_pending = 0; emit(KEY_KEY3, KEYEV_SHORT, 0); }
    if (keys[KEY_BOOT].down && keys[KEY_KEY3].down) {
        if (keys[KEY_BOOT].waking || keys[KEY_KEY3].waking) return;
        if (!recovery && now - keys[KEY_BOOT].at >= 3000 && now - keys[KEY_KEY3].at >= 3000) {
            recovery = true; keys[KEY_BOOT].consumed = keys[KEY_KEY3].consumed = true;
            emit(KEY_BOOT, KEYEV_RECOVERY, 3000);
        }
        return;
    }
    recovery = false;
    for (unsigned key = 0; key < KEY_COUNT; ++key) if (keys[key].down) {
        if (keys[key].waking && key != KEY_PWR && key != KEY_KEY3) continue;
        uint32_t held = now - keys[key].at;
        if (key == KEY_PWR) {
            if (held >= 4000) { emit(key, KEYEV_POWER_OFF, held); keys[key].consumed = true; }
            else if (held >= 2000 && !keys[key].consumed) { emit(key, KEYEV_POWER_MENU, held); keys[key].consumed = true; }
        } else if (held >= 1000 && !keys[key].consumed) {
            emit(key, KEYEV_LONG, held); keys[key].consumed = true;
        }
        emit(key, KEYEV_HOLD, held);
    }
}
static void events(void)
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) sim_running = false;
        if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
            SDL_Keycode code = event.key.keysym.sym; bool down = event.type == SDL_KEYDOWN;
            if (down && code == SDLK_ESCAPE) sim_running = false;
            if (code >= SDLK_1 && code <= SDLK_3) key_event((key_id_t)(code - SDLK_1), down);
        }
        if (event.type == SDL_MOUSEMOTION) { mouse_x = event.motion.x; mouse_y = event.motion.y; }
        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
            mouse_down = true; mouse_x = event.button.x; mouse_y = event.button.y;
            if (!power_screen_on()) { power_activity(); drop_mouse = true; }
        }
        if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
            mouse_x = event.button.x; mouse_y = event.button.y;
            mouse_down = drop_mouse = false;
        }
        if (event.type == SDL_MOUSEWHEEL && power_screen_on()) {
            // The desktop wheel scrolls the same LVGL container as touch dragging.
            lv_obj_t *object = lv_indev_get_scroll_obj(pointer_device);
            if (object) lv_obj_scroll_by(object, 0, event.wheel.y * 40, LV_ANIM_OFF);
        }
    }
}
static void screenshot(const char *path)
{
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 480, 480, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surface || SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, surface->pixels, surface->pitch) || SDL_SaveBMP(surface, path)) {
        fprintf(stderr, "Screenshot failed: %s\n", SDL_GetError()); exit(1);
    }
    SDL_FreeSurface(surface); printf("Screenshot saved.\n");
}
int main(int argc, char **argv)
{
    const char *card = "clock", *state_name = "normal", *shot = NULL, *language = "zh-CN", *open = NULL;
    const char *examples = NUKA_SOURCE_ROOT "/protocol/examples/device";
    const char *assets = NUKA_SOURCE_ROOT "/build/nukapack.bin";
    const char *face="clock",*pose=NULL,*presses=NULL,*record_directory=NULL;
    bool minimal = false,assistant=false; unsigned frames = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--minimal")) minimal = true;
        else if(!strcmp(argv[i],"--enable-assistant"))assistant=true;
        else if (!strcmp(argv[i], "--help")) {
            puts("nuka_sim [--card ID] [--state normal|loading|offline|unconfigured] [--language zh-CN|en] [--minimal] [--examples DIR] [--assets FILE] [--frames N] [--screenshot FILE.bmp]\n         [--open detail|arrange|power|quick|notifications|permission|critical|ota|toast|recovery]\n         [--face clock|assistant] [--pose idle|attention|listening|thinking|speaking|happy|busy|worried|hungry|sleepy|charging|dizzy|offline|error]\n         [--enable-assistant] [--presses KEY:MILLISECONDS,...] [--battery PCT] [--record-dir DIR]\nKeys: 1=PWR, 2=BOOT, 3=Key3; mouse=touch; Esc=exit."); return 0;
        } else {
            if (i + 1 == argc) { fputs("Missing argument value\n", stderr); return 1; }
            const char *option = argv[i++], *value = argv[i];
            if (!strcmp(option, "--card")) card = value;
            else if (!strcmp(option, "--state")) state_name = value;
            else if (!strcmp(option, "--screenshot")) shot = value;
            else if (!strcmp(option, "--assets")) assets = value;
            else if (!strcmp(option, "--examples")) examples = value;
            else if (!strcmp(option, "--language")) language = value;
            else if (!strcmp(option, "--open")) open = value;
            else if(!strcmp(option,"--face"))face=value;
            else if(!strcmp(option,"--pose"))pose=value;
            else if(!strcmp(option,"--presses"))presses=value;
            else if(!strcmp(option,"--record-dir"))record_directory=value;
            else if(!strcmp(option,"--battery")){
                char *end;unsigned long pct=strtoul(value,&end,10);
                if(*end||pct>100||*value=='-'){fputs("Invalid battery percentage\n",stderr);return 1;}
                sim_battery_pct=(unsigned)pct;
            }
            else if (!strcmp(option, "--frames")) {
                char *end; unsigned long count = strtoul(value, &end, 10);
                if (*end || !count || *value == '-' || count > UINT_MAX) { fputs("Invalid frame count\n", stderr); return 1; }
                frames = count;
            } else { fprintf(stderr, "Unknown option %s\n", option); return 1; }
        }
    }
    const char *states[] = { "normal", "loading", "offline", "unconfigured" };
    unsigned state = 0; while (state < 4 && strcmp(state_name, states[state])) ++state;
    bool found = false; for (unsigned i = 0; i < nuka_app_count; ++i) if (!strcmp(card, nuka_apps[i]->id)) found = true;
    if (state == 4 || !found || (strcmp(language, "zh-CN") && strcmp(language, "en"))) { fputs("Invalid card, state or language\n", stderr); return 1; }
    if(strcmp(face,"clock")&&strcmp(face,"assistant")){fputs("Invalid home face\n",stderr);return 1;}
    if(pose&&!face_view_preview(pose)){fputs("Invalid face pose\n",stderr);return 1;}
    struct {key_id_t key;unsigned duration;} sequence[32];unsigned sequence_count=0;
    if(presses){
        const char *at=presses;
        while(*at){
            char *end;unsigned long key=strtoul(at,&end,10);
            if(key<1||key>3||end==at||*end!=':'||sequence_count==32){fputs("Invalid --presses sequence\n",stderr);return 1;}
            at=end+1;unsigned long duration=strtoul(at,&end,10);
            if(!duration||duration>60000||end==at||(*end&&*end!=',')){fputs("Invalid press duration\n",stderr);return 1;}
            sequence[sequence_count].key=(key_id_t)(key-1);sequence[sequence_count++].duration=(unsigned)duration;
            at=*end?end+1:end;
        }
    }
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER)) { fprintf(stderr, "%s\n", SDL_GetError()); return 1; }
    SDL_Window *window = SDL_CreateWindow("nuka-esp32 simulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 480, 480, 0);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!window || !renderer) { fprintf(stderr, "%s\n", SDL_GetError()); return 1; }
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 480, 480);
    if (!texture) { fprintf(stderr, "%s\n", SDL_GetError()); return 1; }
    ESP_ERROR_CHECK(store_init()); vm_store_init(); sim_load_examples(examples, minimal); sim_assets(assets);
    ESP_ERROR_CHECK(vault_init()); ESP_ERROR_CHECK(pomodoro_init()); lv_init(); lv_tick_set_cb(SDL_GetTicks);
    ESP_ERROR_CHECK(nukapack_init()); strcpy(nuka_settings.lang, language); i18n_set_language(language);
    strcpy(nuka_settings.home_face,face);nuka_settings.assistant=assistant;
    lv_display_t *display = lv_display_create(480, 480); lv_display_set_color_format(display, LV_COLOR_FORMAT_ARGB8888);
    lv_display_set_buffers(display, draw_buffer, NULL, sizeof(draw_buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    pointer_device = lv_indev_create(); lv_indev_set_type(pointer_device, LV_INDEV_TYPE_POINTER); lv_indev_set_read_cb(pointer_device, pointer_read);
    theme_init(); nav_init(); overlay_init(); keynav_init(); nav_show(card); nav_set_state((view_state_t)state);
    if(pose){
        if(!strcmp(pose,"speaking"))home_voice_set(HOME_VOICE_SPEAKING,"",0);
        else if(!strcmp(pose,"thinking"))home_voice_set(HOME_VOICE_THINKING,"",0);
        else if(!strcmp(pose,"listening"))home_voice_set(HOME_VOICE_LISTENING,"",0);
        else if(!strcmp(pose,"error"))home_voice_set(HOME_VOICE_ERROR,tr("voice.offline"),0);
    }
    cJSON *initial = nav_current()->topic == VM_NONE ? NULL : vm_store_snapshot(nav_current()->topic);
    nav_current()->view->apply(initial, (view_state_t)state); cJSON_Delete(initial);
    // Screenshot reviews reach L1 pages and overlays without scripting input.
    if (open) {
        if (!strcmp(open, "detail")) nav_detail(); else if (!strcmp(open, "arrange")) nav_arrange(); else if (!strcmp(open, "power")) overlay_power();
        else if(!strcmp(open,"recovery"))overlay_recovery();
        else if (!strcmp(open, "quick")) overlay_quick(); else if (!strcmp(open, "notifications")) overlay_notifications();
        else if (!strcmp(open, "toast")) overlay_toast(tr("permission.done"));
        else if (!strcmp(open, "permission") || !strcmp(open, "critical") || !strcmp(open, "ota")) {
            cJSON *data = sim_example(examples, !strcmp(open, "permission") ? "perm.req" : !strcmp(open, "critical") ? "notify.push" : "ota.offer");
            // Example timestamps are fixed; keep the request open and the notification critical for the shot.
            if (!strcmp(open, "permission")) { cJSON_ReplaceItemInObjectCaseSensitive(data, "expires_at", cJSON_CreateNumber((double)time(NULL) + 600)); overlay_permission(data); }
            else if (!strcmp(open, "critical")) { cJSON_ReplaceItemInObjectCaseSensitive(data, "level", cJSON_CreateString("critical")); overlay_notification(data); }
            else overlay_ota(data, overlay_dismissed);
            cJSON_Delete(data);
        } else { fputs("Invalid --open target\n", stderr); return 1; }
    }
    unsigned frame = 0,press_index=0; uint32_t second = 0,next_press=SDL_GetTicks()+300;bool scripted_down=false;
    while (sim_running && (!frames || frame < frames)) {
        if(press_index<sequence_count&&SDL_GetTicks()>=next_press){
            if(!scripted_down){key_event(sequence[press_index].key,true);scripted_down=true;next_press=SDL_GetTicks()+sequence[press_index].duration;}
            else{key_event(sequence[press_index].key,false);scripted_down=false;++press_index;next_press=SDL_GetTicks()+100;}
        }
        events(); sim_dispatch_events(); key_tick(); nav_tick(); overlay_tick(); vault_app_tick();
        ESP_ERROR_CHECK(pomodoro_tick());
        if (SDL_GetTicks() - second >= 1000 && !vault_app_model()->busy) { ESP_ERROR_CHECK(vault_tick()); second = SDL_GetTicks(); }
        lv_timer_handler();
        SDL_UpdateTexture(texture, NULL, pixels, 480 * 4); SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); SDL_RenderClear(renderer);
        if (power_screen_on()) SDL_RenderCopy(renderer, texture, NULL, NULL);
        if (shot && frames && frame + 1 == frames) screenshot(shot);
        if(record_directory&&frame%4==0){char path[4096];snprintf(path,sizeof(path),"%s/%06u.bmp",record_directory,frame);screenshot(path);}
        SDL_RenderPresent(renderer); ++frame; SDL_Delay(16);
    }
    if (shot && !frames) screenshot(shot);
    printf("[sim] final card=%s detail=%d face=%s voice=%d\n",nav_current()->id,nav_in_detail(),home_assistant_face()?"assistant":"clock",home_voice_state());
    unsigned face_frames;bool face_active;face_view_preview_stats(&face_frames,&face_active);
    printf("[sim] face renders=%u timer_active=%d screen=%d battery=%u\n",face_frames,face_active,power_screen_on(),sim_battery_pct);
    vault_lock(); SDL_DestroyTexture(texture); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window);
    // The PIN worker is process-scoped; exit ends it after all sensitive state has been cleared.
    return 0;
}
