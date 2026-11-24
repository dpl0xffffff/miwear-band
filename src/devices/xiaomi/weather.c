#include "salus/devices/xiaomi/weather.h"

#include "salus/devices/xiaomi/wire_internal.h"
#include "salus/services/weather.h"

#include <string.h>
#include <time.h>

#include "xiaomi.pb-c.h"

/* Encoders for the Xiaomi weather protocol (type 10). Fetching is the weather service's job. */

static uint32_t wmo_to_xiaomi_condition(int code) {
    if (code == 0) {
        return 0;
    }
    if (code == 1 || code == 2) {
        return 1;
    }
    if (code == 3) {
        return 2;
    }
    if (code >= 51 && code <= 57) {
        return 7;
    }
    if (code >= 61 && code <= 67) {
        return 8;
    }
    if (code >= 71 && code <= 77) {
        return 14;
    }
    if (code >= 80 && code <= 82) {
        return 3;
    }
    if (code >= 95) {
        return 4;
    }
    if (code == 45 || code == 48) {
        return 18;
    }
    return 1;
}

static int beaufort_from_kmh(int kmh) {
    if (kmh < 2) return 0;
    if (kmh < 6) return 1;
    if (kmh < 12) return 2;
    if (kmh < 20) return 3;
    if (kmh < 29) return 4;
    if (kmh < 39) return 5;
    if (kmh < 50) return 6;
    if (kmh < 62) return 7;
    if (kmh < 75) return 8;
    if (kmh < 89) return 9;
    if (kmh < 103) return 10;
    if (kmh < 118) return 11;
    return 12;
}

static gchar *iso_colon_timestamp(time_t t) {
    GDateTime *dt = g_date_time_new_from_unix_utc((gint64)t);
    if (dt == NULL) {
        return g_strdup("");
    }
    gchar *out = g_date_time_format(dt, "%Y-%m-%dT%H:%M:%S");
    g_date_time_unref(dt);
    return out;
}

static void send_weather_prefs(SalusApp *state) {
    Xiaomi__WeatherPrefs prefs = XIAOMI__WEATHER_PREFS__INIT;
    prefs.has_temperaturescale = TRUE;
    prefs.temperaturescale = 1;

    Xiaomi__Weather wx = XIAOMI__WEATHER__INIT;
    wx.prefs = &prefs;

    Xiaomi__Command cmd = XIAOMI__COMMAND__INIT;
    cmd.type = 10;
    cmd.has_subtype = TRUE;
    cmd.subtype = 10;
    cmd.weather = &wx;

    (void)salus_wire_send_xiaomi_command(state, &cmd, "Weather prefs (°C)");
}

static void send_add_location(SalusApp *state, const char *code, const char *name) {
    Xiaomi__WeatherLocation loc = XIAOMI__WEATHER_LOCATION__INIT;
    loc.code = (gchar *)code;
    loc.name = (gchar *)name;

    Xiaomi__Weather wx = XIAOMI__WEATHER__INIT;
    wx.location = &loc;

    Xiaomi__Command cmd = XIAOMI__COMMAND__INIT;
    cmd.type = 10;
    cmd.has_subtype = TRUE;
    cmd.subtype = 7;
    cmd.weather = &wx;

    (void)salus_wire_send_xiaomi_command(state, &cmd, "Weather add location");
}

static void send_current_weather(SalusApp *state,
                                 const char *city,
                                 const char *loc_key,
                                 const SalusWeather *snap) {
    if (snap == NULL) {
        return;
    }

    gchar *pub = iso_colon_timestamp(time(NULL));
    Xiaomi__WeatherMetadata meta = XIAOMI__WEATHER_METADATA__INIT;
    meta.publicationtimestamp = pub;
    meta.cityname = (gchar *)"";
    meta.locationname = (gchar *)city;
    meta.locationkey = (gchar *)loc_key;
    meta.has_iscurrentlocation = TRUE;
    meta.iscurrentlocation = TRUE;

    Xiaomi__WeatherUnitValue temp = XIAOMI__WEATHER_UNIT_VALUE__INIT;
    temp.unit = (gchar *)"℃";
    temp.value = snap->temp_c;
    Xiaomi__WeatherUnitValue hum = XIAOMI__WEATHER_UNIT_VALUE__INIT;
    hum.unit = (gchar *)"%";
    hum.value = snap->humidity;
    Xiaomi__WeatherUnitValue wind = XIAOMI__WEATHER_UNIT_VALUE__INIT;
    wind.unit = g_strdup_printf("%d", snap->wind_direction);
    wind.value = beaufort_from_kmh(snap->wind_speed_kmh);
    Xiaomi__WeatherUnitValue uv = XIAOMI__WEATHER_UNIT_VALUE__INIT;
    uv.unit = (gchar *)"";
    uv.value = 0;
    Xiaomi__WeatherUnitValue aqi = XIAOMI__WEATHER_UNIT_VALUE__INIT;
    aqi.unit = (gchar *)"Unknown";
    aqi.value = 0;
    Xiaomi__WeatherWarnings warns = XIAOMI__WEATHER_WARNINGS__INIT;

    Xiaomi__WeatherCurrent cur = XIAOMI__WEATHER_CURRENT__INIT;
    cur.metadata = &meta;
    cur.weathercondition = wmo_to_xiaomi_condition(snap->wmo_code);
    cur.temperature = &temp;
    cur.humidity = &hum;
    cur.wind = &wind;
    cur.uv = &uv;
    cur.aqi = &aqi;
    cur.warning = &warns;
    cur.has_pressure = TRUE;
    cur.pressure = (float)snap->pressure_hpa;

    Xiaomi__Weather wx = XIAOMI__WEATHER__INIT;
    wx.current = &cur;

    Xiaomi__Command cmd = XIAOMI__COMMAND__INIT;
    cmd.type = 10;
    cmd.has_subtype = TRUE;
    cmd.subtype = 0;
    cmd.weather = &wx;

    (void)salus_wire_send_xiaomi_command(state, &cmd, "Weather current conditions");
    g_free(wind.unit);
    g_free(pub);
}

static void send_daily_forecast(SalusApp *state,
                                const char *city,
                                const char *loc_key,
                                const SalusWeather *snap) {
    if (snap == NULL || snap->n_daily <= 0) {
        return;
    }

    gchar *pub = iso_colon_timestamp(time(NULL));
    Xiaomi__WeatherMetadata meta = XIAOMI__WEATHER_METADATA__INIT;
    meta.publicationtimestamp = pub;
    meta.cityname = (gchar *)"";
    meta.locationname = (gchar *)city;
    meta.locationkey = (gchar *)loc_key;
    meta.has_iscurrentlocation = TRUE;
    meta.iscurrentlocation = TRUE;

    const int n = snap->n_daily > 6 ? 6 : snap->n_daily;
    Xiaomi__ForecastEntry **entry_ptrs = g_malloc0((gsize)n * sizeof(Xiaomi__ForecastEntry *));
    Xiaomi__ForecastEntry *entries = g_malloc0((gsize)n * sizeof(Xiaomi__ForecastEntry));
    Xiaomi__WeatherRange *cond_ranges = g_malloc0((gsize)n * sizeof(Xiaomi__WeatherRange));
    Xiaomi__WeatherRange *temp_ranges = g_malloc0((gsize)n * sizeof(Xiaomi__WeatherRange));
    Xiaomi__WeatherSunriseSunset *sun = g_malloc0((gsize)n * sizeof(Xiaomi__WeatherSunriseSunset));
    Xiaomi__WeatherUnitValue *aqis = g_malloc0((gsize)n * sizeof(Xiaomi__WeatherUnitValue));

    for (int i = 0; i < n; i++) {
        xiaomi__forecast_entry__init(&entries[i]);
        xiaomi__weather_range__init(&cond_ranges[i]);
        xiaomi__weather_range__init(&temp_ranges[i]);
        xiaomi__weather_sunrise_sunset__init(&sun[i]);
        xiaomi__weather_unit_value__init(&aqis[i]);

        const uint32_t xc = wmo_to_xiaomi_condition(snap->daily_code[i]);
        cond_ranges[i].from = (int32_t)xc;
        cond_ranges[i].to = (int32_t)xc;
        temp_ranges[i].from = snap->daily_max[i];
        temp_ranges[i].to = snap->daily_min[i];
        aqis[i].unit = (gchar *)"Unknown";
        aqis[i].value = 0;
        sun[i].sunrise = (gchar *)(snap->sunrise[i] != NULL ? snap->sunrise[i] : "");
        sun[i].sunset = (gchar *)(snap->sunset[i] != NULL ? snap->sunset[i] : "");

        entries[i].aqi = &aqis[i];
        entries[i].conditionrange = &cond_ranges[i];
        entries[i].temperaturerange = &temp_ranges[i];
        entries[i].temperaturesymbol = (gchar *)"℃";
        entries[i].sunrisesunset = &sun[i];
        entry_ptrs[i] = &entries[i];
    }

    Xiaomi__ForecastEntries fentries = XIAOMI__FORECAST_ENTRIES__INIT;
    fentries.n_entry = (size_t)n;
    fentries.entry = entry_ptrs;

    Xiaomi__WeatherForecast forecast = XIAOMI__WEATHER_FORECAST__INIT;
    forecast.metadata = &meta;
    forecast.entries = &fentries;

    Xiaomi__Weather wx = XIAOMI__WEATHER__INIT;
    wx.forecast = &forecast;

    Xiaomi__Command cmd = XIAOMI__COMMAND__INIT;
    cmd.type = 10;
    cmd.has_subtype = TRUE;
    cmd.subtype = 1;
    cmd.weather = &wx;

    (void)salus_wire_send_xiaomi_command(state, &cmd, "Weather daily forecast");
    g_free(pub);
    g_free(entry_ptrs);
    g_free(entries);
    g_free(cond_ranges);
    g_free(temp_ranges);
    g_free(sun);
    g_free(aqis);
}

void xiaomi_weather_push(SalusDevice *dev, const SalusWeather *w) {
    SalusApp *state = dev->app;
    send_weather_prefs(state);
    send_add_location(state, w->location_key, w->location_name);
    send_current_weather(state, w->location_name, w->location_key, w);
    send_daily_forecast(state, w->location_name, w->location_key, w);
}

void xiaomi_weather_handle_command(SalusApp *state, const Xiaomi__Command *cmd) {
    if (cmd->type == 10 && cmd->has_subtype && cmd->subtype == 3) {
        append_log(state, "Band requested weather update — fetching Open-Meteo…");
        salus_weather_refresh(state);
    }
}
