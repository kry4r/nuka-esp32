// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app.h"
extern const app_view_t weather_view;
static void action(app_action_t a){}
const app_def_t weather_app={"weather",VM_WEATHER,&weather_view,action};
