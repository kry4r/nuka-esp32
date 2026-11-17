// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app.h"
#include "overlay.h"
#include "nav.h"
#include "home_app.h"
#include "store.h"
#include <string.h>
#include <stdio.h>
extern const app_view_t clock_view;
bool clock_show_seconds;
static bool face, initialized;
static char configured_face[10], caption_text[192];
static home_voice_state_t voice_state;
static uint32_t error_until;
static uint8_t voice_level;
bool home_assistant_face(void)
{
    if (!initialized || strcmp(configured_face,nuka_settings.home_face)) {
        initialized=true;snprintf(configured_face,sizeof(configured_face),"%s",nuka_settings.home_face);
        face=!strcmp(configured_face,"assistant");
    }
    return face || home_voice_state()!=HOME_VOICE_IDLE;
}
void home_toggle_face(void) { home_assistant_face();face=!face;nav_refresh(); }
void home_voice_set(home_voice_state_t state,const char *caption,uint8_t level)
{
    voice_state=state;voice_level=level;error_until=lv_tick_get()+3000;snprintf(caption_text,sizeof(caption_text),"%s",caption);nav_refresh();
}
home_voice_state_t home_voice_state(void) { if(voice_state==HOME_VOICE_ERROR&&lv_tick_get()>=error_until){voice_state=HOME_VOICE_IDLE;caption_text[0]=0;}return voice_state; }
const char *home_voice_caption(void) { return caption_text; }
uint8_t home_voice_level(void) { return voice_level; }
static void action(app_action_t action)
{
    if (action == ACT_PRIMARY) overlay_quick();
    if (action == ACT_SECONDARY) { clock_show_seconds = !clock_show_seconds; nav_refresh(); }
}
const app_def_t clock_app = {"clock", VM_NONE, &clock_view, action};
