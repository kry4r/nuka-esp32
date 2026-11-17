// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app.h"
extern const app_def_t clock_app,hosts_app,servers_app,ai_app,buddy_app,
    pomodoro_app,media_app,weather_app,vault_app,settings_app;
const app_def_t *nuka_apps[]={&clock_app,&hosts_app,&servers_app,&ai_app,&buddy_app,
    &pomodoro_app,&media_app,&weather_app,&vault_app,&settings_app};
const unsigned nuka_app_count=sizeof(nuka_apps)/sizeof(nuka_apps[0]);
