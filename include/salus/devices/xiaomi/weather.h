#ifndef SALUS_DEVICES_XIAOMI_WEATHER_H
#define SALUS_DEVICES_XIAOMI_WEATHER_H

#include "salus/app.h"
#include "salus/services/weather.h"

#include "xiaomi.pb-c.h"

/* push_weather op: prefs, location, current conditions and the daily forecast. */
void xiaomi_weather_push(SalusDevice *dev, const SalusWeather *w);
/* The band asks for fresh weather (type 10 subtype 3). */
void xiaomi_weather_handle_command(SalusApp *state, const Xiaomi__Command *cmd);

#endif
