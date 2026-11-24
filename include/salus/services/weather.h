#ifndef SALUS_SERVICES_WEATHER_H
#define SALUS_SERVICES_WEATHER_H

/*
 * Weather for devices that show it: fetched from Open-Meteo for the [weather] coordinates in
 * config.ini and handed to the active device's push_weather op.
 */

#include "salus/app.h"

#define SALUS_WEATHER_DAYS 7

typedef struct SalusWeather {
    const char *location_name; /* "Home" */
    const char *location_key;  /* stable id derived from the name */
    int temp_c;
    int humidity;              /* % */
    int wmo_code;              /* WMO weather interpretation code */
    int pressure_hpa;
    int wind_speed_kmh;
    int wind_direction;        /* degrees */
    int n_daily;
    int daily_max[SALUS_WEATHER_DAYS];
    int daily_min[SALUS_WEATHER_DAYS];
    int daily_code[SALUS_WEATHER_DAYS];
    const char *sunrise[SALUS_WEATHER_DAYS]; /* ISO 8601 local time */
    const char *sunset[SALUS_WEATHER_DAYS];
} SalusWeather;

/* Fetches the weather (asynchronously) and pushes it to the connected device. */
void salus_weather_refresh(SalusApp *app);
/* Cancels an in-flight fetch (app shutdown). */
void salus_weather_shutdown(SalusApp *app);

#endif
