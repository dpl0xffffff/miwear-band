#include "salus/services/weather.h"

#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include <math.h>
#include <string.h>

/* Parsed Open-Meteo response; owns its strings. */
typedef struct {
    SalusWeather w;
    gchar *location_name;
    gchar *location_key;
    gchar *sunrise[SALUS_WEATHER_DAYS];
    gchar *sunset[SALUS_WEATHER_DAYS];
} WeatherData;

static void weather_data_free(WeatherData *d) {
    if (d == NULL) {
        return;
    }
    for (int i = 0; i < SALUS_WEATHER_DAYS; i++) {
        g_free(d->sunrise[i]);
        g_free(d->sunset[i]);
    }
    g_free(d->location_name);
    g_free(d->location_key);
    g_free(d);
}

static gchar *location_key_from_name(const char *name) {
    unsigned h = 0;
    for (const unsigned char *p = (const unsigned char *)(name != NULL ? name : ""); *p; p++) {
        h = h * 31u + (unsigned)*p;
    }
    return g_strdup_printf("accu:%u", h % 1000000u);
}

static void set_weather_status(SalusApp *app, const char *msg) {
    g_free(app->weather_last_status);
    app->weather_last_status = g_strdup(msg);
}

static WeatherData *parse_open_meteo(const char *body, const char *location_name) {
    JsonParser *parser = json_parser_new();
    if (!json_parser_load_from_data(parser, body, -1, NULL)) {
        g_object_unref(parser);
        return NULL;
    }
    JsonNode *root = json_parser_get_root(parser);
    if (root == NULL || !JSON_NODE_HOLDS_OBJECT(root)) {
        g_object_unref(parser);
        return NULL;
    }
    JsonObject *obj = json_node_get_object(root);
    WeatherData *d = g_new0(WeatherData, 1);
    d->location_name = g_strdup(location_name);
    d->location_key = location_key_from_name(location_name);
    SalusWeather *w = &d->w;
    w->location_name = d->location_name;
    w->location_key = d->location_key;

    if (json_object_has_member(obj, "current")) {
        JsonObject *cur = json_object_get_object_member(obj, "current");
        w->temp_c = (int)lround(json_object_get_double_member(cur, "temperature_2m"));
        w->humidity = (int)json_object_get_int_member(cur, "relative_humidity_2m");
        w->wmo_code = (int)json_object_get_int_member(cur, "weather_code");
        w->pressure_hpa = (int)lround(json_object_get_double_member(cur, "pressure_msl"));
        w->wind_speed_kmh = (int)lround(json_object_get_double_member(cur, "wind_speed_10m"));
        w->wind_direction = (int)json_object_get_int_member(cur, "wind_direction_10m");
    }
    if (json_object_has_member(obj, "daily")) {
        JsonObject *daily = json_object_get_object_member(obj, "daily");
        JsonArray *codes = json_object_get_array_member(daily, "weather_code");
        JsonArray *tmax = json_object_get_array_member(daily, "temperature_2m_max");
        JsonArray *tmin = json_object_get_array_member(daily, "temperature_2m_min");
        JsonArray *sr = json_object_get_array_member(daily, "sunrise");
        JsonArray *ss = json_object_get_array_member(daily, "sunset");
        const guint n = codes != NULL ? json_array_get_length(codes) : 0;
        w->n_daily = (int)MIN(n, (guint)SALUS_WEATHER_DAYS);
        for (int i = 0; i < w->n_daily; i++) {
            w->daily_code[i] = (int)json_array_get_int_element(codes, (guint)i);
            w->daily_max[i] = (int)lround(json_array_get_double_element(tmax, (guint)i));
            w->daily_min[i] = (int)lround(json_array_get_double_element(tmin, (guint)i));
            const char *sr_s = json_array_get_string_element(sr, (guint)i);
            const char *ss_s = json_array_get_string_element(ss, (guint)i);
            d->sunrise[i] = g_strdup(sr_s != NULL ? sr_s : "");
            d->sunset[i] = g_strdup(ss_s != NULL ? ss_s : "");
            w->sunrise[i] = d->sunrise[i];
            w->sunset[i] = d->sunset[i];
        }
    }
    g_object_unref(parser);
    return d;
}

typedef struct {
    SalusApp *app;
    gchar *location_name;
} FetchCtx;

static void fetch_ctx_free(FetchCtx *ctx) {
    g_free(ctx->location_name);
    g_free(ctx);
}

/* Main thread: hand the forecast to the device that is connected now (if any). */
static void apply_fetch(SalusApp *app, const char *location_name, const gchar *body, const char *error) {
    WeatherData *d = body != NULL ? parse_open_meteo(body, location_name) : NULL;
    gchar *line = NULL;
    if (d == NULL) {
        line = g_strdup_printf("Weather fetch failed: %s", error != NULL ? error : "Open-Meteo JSON parse failed");
    } else if (salus_device_connected(app) && app->device->ops->push_weather != NULL) {
        app->device->ops->push_weather(app->device, &d->w);
        line = g_strdup_printf("Weather pushed for %s", location_name);
    } else {
        line = g_strdup("Weather fetched but no device connected; push skipped.");
    }
    set_weather_status(app, line);
    append_log(app, line);
    g_free(line);
    weather_data_free(d);
}

static void on_curl_done(GObject *source, GAsyncResult *res, gpointer user_data) {
    FetchCtx *ctx = user_data;
    GSubprocess *proc = G_SUBPROCESS(source);
    gchar *out = NULL, *err_out = NULL;
    GError *err = NULL;
    const gboolean ok = g_subprocess_communicate_utf8_finish(proc, res, &out, &err_out, &err);
    if (g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        /* App shutdown: SalusApp may already be torn down. */
        g_subprocess_force_exit(proc);
    } else {
        ctx->app->weather_fetch_inflight = FALSE;
        if (ok && g_subprocess_get_successful(proc)) {
            apply_fetch(ctx->app, ctx->location_name, out, NULL);
        } else {
            apply_fetch(ctx->app, ctx->location_name, NULL,
                        err != NULL ? err->message
                        : err_out != NULL && err_out[0] != '\0' ? err_out
                                                                : "curl failed");
        }
    }
    g_clear_error(&err);
    g_free(out);
    g_free(err_out);
    fetch_ctx_free(ctx);
}

void salus_weather_refresh(SalusApp *app) {
    if (!salus_device_connected(app) || !salus_device_has(app, SALUS_CAP_WEATHER)) {
        return;
    }
    if (!app->weather_coords_valid) {
        set_weather_status(app, "Configure [weather] latitude/longitude in ~/.config/salus/config.ini");
        append_log(app, "Weather: missing coordinates in config.ini ([weather] latitude/longitude).");
        return;
    }
    if (app->weather_fetch_inflight) {
        append_log(app, "Weather fetch already in progress.");
        return;
    }
    /* Asynchronous: curl may take up to its 20 s timeout. */
    gchar *url = g_strdup_printf(
        "https://api.open-meteo.com/v1/forecast?latitude=%.5f&longitude=%.5f"
        "&current=temperature_2m,relative_humidity_2m,weather_code,pressure_msl,wind_speed_10m,wind_direction_10m"
        "&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset"
        "&timezone=auto&forecast_days=7",
        app->weather_latitude,
        app->weather_longitude);
    GError *err = NULL;
    GSubprocess *proc = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE, &err,
                                         "curl", "-sS", "-f", "--max-time", "20", url, NULL);
    g_free(url);
    if (proc == NULL) {
        apply_fetch(app, NULL, NULL, err != NULL ? err->message : "cannot run curl");
        g_clear_error(&err);
        return;
    }
    FetchCtx *ctx = g_new0(FetchCtx, 1);
    ctx->app = app;
    ctx->location_name = g_strdup(app->weather_location_name != NULL ? app->weather_location_name : "Home");
    if (app->weather_cancellable == NULL) {
        app->weather_cancellable = g_cancellable_new();
    }
    app->weather_fetch_inflight = TRUE;
    g_subprocess_communicate_utf8_async(proc, NULL, app->weather_cancellable, on_curl_done, ctx);
    g_object_unref(proc); /* the pending operation keeps its own reference */
}

void salus_weather_shutdown(SalusApp *app) {
    if (app->weather_cancellable != NULL) {
        g_cancellable_cancel(app->weather_cancellable);
        g_clear_object(&app->weather_cancellable);
    }
    app->weather_fetch_inflight = FALSE;
}
