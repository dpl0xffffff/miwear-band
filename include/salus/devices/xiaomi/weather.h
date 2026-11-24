#ifndef SALUS_SPP_WEATHER_H
#define SALUS_SPP_WEATHER_H

#include "salus/app.h"

#include "xiaomi.pb-c.h"

void salus_weather_initialize(AppState *state);
void salus_weather_handle_command(AppState *state, const Xiaomi__Command *cmd);
void salus_weather_push_cached(AppState *state);
/* Cancels an in-flight weather fetch (app shutdown). */
void salus_weather_shutdown(AppState *state);

#endif
