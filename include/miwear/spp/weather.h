#ifndef MIWEAR_SPP_WEATHER_H
#define MIWEAR_SPP_WEATHER_H

#include "miwear/app.h"

#include "xiaomi.pb-c.h"

void miwear_weather_initialize(AppState *state);
void miwear_weather_handle_command(AppState *state, const Xiaomi__Command *cmd);
void miwear_weather_push_cached(AppState *state);

#endif
