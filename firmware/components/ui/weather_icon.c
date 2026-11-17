// SPDX-License-Identifier: AGPL-3.0-or-later
#include "weather_icon.h"
#include <string.h>
ui_icon_t ui_weather_icon(const char *icon)
{
    static const struct { const char *name; ui_icon_t icon; } map[] = {
        {"clear_day", ICON_SUN}, {"clear_night", ICON_NIGHT}, {"partly_cloudy", ICON_PARTLY}, {"cloudy", ICON_CLOUD},
        {"fog", ICON_FOG}, {"drizzle", ICON_DRIZZLE}, {"rain", ICON_RAIN}, {"heavy_rain", ICON_HEAVY_RAIN},
        {"snow", ICON_SNOW}, {"sleet", ICON_SLEET}, {"thunder", ICON_THUNDER}, {"wind", ICON_WIND},
    };
    for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); ++i) if (!strcmp(map[i].name, icon)) return map[i].icon;
    return ICON_CLOUD;
}
