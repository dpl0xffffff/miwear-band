#include "miwear/spp/weather.h"
#include "miwear/spp/wire_internal.h"

#include <math.h>
#include <string.h>
#include <time.h>

#include <gio/gio.h>
#include <json-glib/json-glib.h>

#include "xiaomi.pb-c.h"

typedef struct {
    AppState *state;
    gchar *location_name;
    gchar *location_code;
} WeatherPushCtx;

typedef struct {
    int temp_c;
    int humidity;
    int wmo_code;
    int pressure_hpa;
    int wind_speed_kmh;
    int wind_direction;
    int daily_max[7];
    int daily_min[7];
    int daily_code[7];
    gchar *sunrise[7];
    gchar *sunset[7];
    int n_daily;
} MeteoSnapshot;

typedef struct {
    MeteoSnapshot snap;
    gchar *location_name;
    gchar *location_code;
} WeatherFetchResult;

static void meteo_snapshot_clear(MeteoSnapshot *s);

static void weather_fetch_result_free(WeatherFetchResult *result) {
    if (result == NULL) {
        return;
    }
    meteo_snapshot_clear(&result->snap);
    g_free(result->location_name);
    g_free(result->location_code);
    g_free(result);
}

static gchar *location_key_from_name(const char *name) {
    unsigned h = 0;
    if (name != NULL) {
        for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
            h = h * 31u + (unsigned)*p;
        }
    }
    return g_strdup_printf("accu:%u", h % 1000000u);
}

static void weather_set_status(AppState *state, const char *msg) {
    if (state == NULL || msg == NULL) {
        return;
    }
    g_free(state->weather_last_status);
    state->weather_last_status = g_strdup(msg);
}

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

static void send_weather_prefs(AppState *state) {
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

    (void)miwear_wire_send_xiaomi_command(state, &cmd, "Weather prefs (°C)");
}

static void send_add_location(AppState *state, const char *code, const char *name) {
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

    (void)miwear_wire_send_xiaomi_command(state, &cmd, "Weather add location");
}

static void send_current_weather(AppState *state,
                                 const char *city,
                                 const char *loc_key,
                                 const MeteoSnapshot *snap) {
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

    (void)miwear_wire_send_xiaomi_command(state, &cmd, "Weather current conditions");
    g_free(wind.unit);
    g_free(pub);
}

static void send_daily_forecast(AppState *state,
                                const char *city,
                                const char *loc_key,
                                const MeteoSnapshot *snap) {
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
        sun[i].sunrise = snap->sunrise[i] != NULL ? snap->sunrise[i] : (gchar *)"";
        sun[i].sunset = snap->sunset[i] != NULL ? snap->sunset[i] : (gchar *)"";

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

    (void)miwear_wire_send_xiaomi_command(state, &cmd, "Weather daily forecast");
    g_free(pub);
    g_free(entry_ptrs);
    g_free(entries);
    g_free(cond_ranges);
    g_free(temp_ranges);
    g_free(sun);
    g_free(aqis);
}

static gboolean parse_open_meteo(const char *body, MeteoSnapshot *out) {
    JsonParser *parser = json_parser_new();
    GError *err = NULL;
    if (!json_parser_load_from_data(parser, body, -1, &err)) {
        g_object_unref(parser);
        g_clear_error(&err);
        return FALSE;
    }
    JsonNode *root = json_parser_get_root(parser);
    if (root == NULL || !JSON_NODE_HOLDS_OBJECT(root)) {
        g_object_unref(parser);
        return FALSE;
    }
    JsonObject *obj = json_node_get_object(root);

    if (json_object_has_member(obj, "current")) {
        JsonObject *cur = json_object_get_object_member(obj, "current");
        out->temp_c = (int)lround(json_object_get_double_member(cur, "temperature_2m"));
        out->humidity = (int)json_object_get_int_member(cur, "relative_humidity_2m");
        out->wmo_code = (int)json_object_get_int_member(cur, "weather_code");
        out->pressure_hpa = (int)lround(json_object_get_double_member(cur, "pressure_msl"));
        out->wind_speed_kmh = (int)lround(json_object_get_double_member(cur, "wind_speed_10m"));
        out->wind_direction = (int)json_object_get_int_member(cur, "wind_direction_10m");
    }

    if (json_object_has_member(obj, "daily")) {
        JsonObject *daily = json_object_get_object_member(obj, "daily");
        JsonArray *codes = json_object_get_array_member(daily, "weather_code");
        JsonArray *tmax = json_object_get_array_member(daily, "temperature_2m_max");
        JsonArray *tmin = json_object_get_array_member(daily, "temperature_2m_min");
        JsonArray *sr = json_object_get_array_member(daily, "sunrise");
        JsonArray *ss = json_object_get_array_member(daily, "sunset");
        const guint n = codes != NULL ? json_array_get_length(codes) : 0;
        out->n_daily = (int)(n > 7 ? 7 : n);
        for (int i = 0; i < out->n_daily; i++) {
            out->daily_code[i] = (int)json_array_get_int_element(codes, (guint)i);
            out->daily_max[i] = (int)lround(json_array_get_double_element(tmax, (guint)i));
            out->daily_min[i] = (int)lround(json_array_get_double_element(tmin, (guint)i));
            const char *sr_s = json_array_get_string_element(sr, (guint)i);
            const char *ss_s = json_array_get_string_element(ss, (guint)i);
            out->sunrise[i] = g_strdup(sr_s != NULL ? sr_s : "");
            out->sunset[i] = g_strdup(ss_s != NULL ? ss_s : "");
        }
    }

    g_object_unref(parser);
    return TRUE;
}

static void meteo_snapshot_clear(MeteoSnapshot *s) {
    if (s == NULL) {
        return;
    }
    for (int i = 0; i < s->n_daily; i++) {
        g_free(s->sunrise[i]);
        g_free(s->sunset[i]);
    }
    memset(s, 0, sizeof(*s));
}

static void weather_push_ctx_free(WeatherPushCtx *ctx) {
    g_free(ctx->location_name);
    g_free(ctx->location_code);
    g_free(ctx);
}

/* Main thread: parse the Open-Meteo response (or report the fetch error) and push to the band. */
static void weather_apply_fetch(AppState *state, WeatherPushCtx *ctx, const gchar *body, GError *err) {
    WeatherFetchResult *result = NULL;
    GError *parse_err = NULL;
    if (body != NULL) {
        result = g_new0(WeatherFetchResult, 1);
        result->location_name = g_strdup(ctx->location_name);
        result->location_code = g_strdup(ctx->location_code);
        if (!parse_open_meteo(body, &result->snap)) {
            weather_fetch_result_free(result);
            result = NULL;
            g_set_error(&parse_err, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Open-Meteo JSON parse failed");
            err = parse_err;
        }
    }

    if (result != NULL && state->auth_initialized && state->sock_fd >= 0) {
        send_weather_prefs(state);
        send_add_location(state, result->location_code, result->location_name);
        send_current_weather(state, result->location_name, result->location_code, &result->snap);
        send_daily_forecast(state, result->location_name, result->location_code, &result->snap);

        gchar *msg = g_strdup_printf("Weather pushed for %s", result->location_name);
        weather_set_status(state, msg);
        append_log(state, msg);
        g_free(msg);
    } else if (err != NULL) {
        gchar *line = g_strdup_printf("Weather fetch failed: %s", err->message);
        weather_set_status(state, line);
        append_log(state, line);
        g_free(line);
    } else if (result != NULL) {
        weather_set_status(state, "Weather fetched but band not connected");
        append_log(state, "Weather fetched but band not connected; push skipped.");
    }

    weather_fetch_result_free(result);
    g_clear_error(&parse_err);
}

static void on_weather_curl_done(GObject *source, GAsyncResult *res, gpointer user_data) {
    WeatherPushCtx *ctx = user_data;
    GSubprocess *proc = G_SUBPROCESS(source);
    gchar *stdout_buf = NULL;
    gchar *stderr_buf = NULL;
    GError *err = NULL;
    const gboolean ok = g_subprocess_communicate_utf8_finish(proc, res, &stdout_buf, &stderr_buf, &err);
    if (g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        /* App shutdown: AppState may already be torn down. */
        g_subprocess_force_exit(proc);
        g_error_free(err);
        g_free(stdout_buf);
        g_free(stderr_buf);
        weather_push_ctx_free(ctx);
        return;
    }

    AppState *state = ctx->state;
    state->weather_fetch_inflight = FALSE;
    if (ok && !g_subprocess_get_successful(proc)) {
        err = g_error_new(G_IO_ERROR, G_IO_ERROR_FAILED, "%s",
                          stderr_buf != NULL && stderr_buf[0] != '\0' ? stderr_buf : "curl failed");
    }
    weather_apply_fetch(state, ctx, err == NULL ? stdout_buf : NULL, err);

    g_clear_error(&err);
    g_free(stdout_buf);
    g_free(stderr_buf);
    weather_push_ctx_free(ctx);
}

/*
 * Fetches Open-Meteo with curl asynchronously. This used to run curl synchronously on the GTK
 * main loop (up to its 20 s timeout), freezing the UI whenever the band asked for weather.
 */
static void schedule_weather_push(AppState *state) {
    if (state == NULL || !state->auth_initialized || state->sock_fd < 0) {
        return;
    }
    if (!state->weather_coords_valid) {
        weather_set_status(state, "Weather coordinates not configured");
        return;
    }
    if (state->weather_fetch_inflight) {
        append_log(state, "Weather fetch already in progress.");
        return;
    }

    gchar *url = g_strdup_printf(
        "https://api.open-meteo.com/v1/forecast?latitude=%.5f&longitude=%.5f"
        "&current=temperature_2m,relative_humidity_2m,weather_code,pressure_msl,wind_speed_10m,wind_direction_10m"
        "&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset"
        "&timezone=auto&forecast_days=7",
        state->weather_latitude,
        state->weather_longitude);
    GError *err = NULL;
    GSubprocess *proc = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE,
                                         &err,
                                         "curl", "-sS", "-f", "--max-time", "20", url, NULL);
    g_free(url);
    if (proc == NULL) {
        gchar *line = g_strdup_printf("Weather fetch failed: %s", err != NULL ? err->message : "cannot run curl");
        weather_set_status(state, line);
        append_log(state, line);
        g_free(line);
        g_clear_error(&err);
        return;
    }

    const char *name = state->weather_location_name != NULL ? state->weather_location_name : "Home";
    WeatherPushCtx *ctx = g_new0(WeatherPushCtx, 1);
    ctx->state = state;
    ctx->location_name = g_strdup(name);
    ctx->location_code = location_key_from_name(name);
    if (state->weather_cancellable == NULL) {
        state->weather_cancellable = g_cancellable_new();
    }
    state->weather_fetch_inflight = TRUE;
    g_subprocess_communicate_utf8_async(proc, NULL, state->weather_cancellable, on_weather_curl_done, ctx);
    g_object_unref(proc); /* the pending operation keeps its own reference */
}

void miwear_weather_shutdown(AppState *state) {
    if (state == NULL || state->weather_cancellable == NULL) {
        return;
    }
    g_cancellable_cancel(state->weather_cancellable);
    g_clear_object(&state->weather_cancellable);
    state->weather_fetch_inflight = FALSE;
}

void miwear_weather_initialize(AppState *state) {
    if (state == NULL) {
        return;
    }
    if (!state->weather_coords_valid) {
        weather_set_status(state, "Configure [weather] latitude/longitude in ~/.config/miwear-band/config.ini");
        append_log(state, "Weather: missing coordinates in config.ini ([weather] latitude/longitude).");
        return;
    }
    schedule_weather_push(state);
}

void miwear_weather_push_cached(AppState *state) {
    schedule_weather_push(state);
}

void miwear_weather_handle_command(AppState *state, const Xiaomi__Command *cmd) {
    if (state == NULL || cmd == NULL || cmd->type != 10) {
        return;
    }
    if (cmd->has_subtype && cmd->subtype == 3) {
        append_log(state, "Band requested weather update — fetching Open-Meteo…");
        schedule_weather_push(state);
    }
}
