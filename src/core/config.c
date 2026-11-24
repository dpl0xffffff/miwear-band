#include "miwear/config.h"
#include "miwear/migrate.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

static const char *CONFIG_GROUP = "device";
static const char *WEATHER_GROUP = "weather";
static const char *NOTIF_GROUP = "notifications";
static const char *LOCATION_GROUP = "location";
static const char *PROFILE_GROUP = "profile";

const char *miwear_config_default_path(void) {
    /* Computed once: callers treat the result as borrowed (it was leaked on every load/save). */
    static gsize once;
    static gchar *path;
    if (g_once_init_enter(&once)) {
        path = salus_config_file_path();
        g_once_init_leave(&once, 1);
    }
    return path;
}

gboolean miwear_config_load(AppState *state, const char *path) {
    const char *cfg_path = path != NULL ? path : miwear_config_default_path();
    GKeyFile *kf = g_key_file_new();
    GError *err = NULL;

    if (!g_key_file_load_from_file(kf, cfg_path, G_KEY_FILE_NONE, &err)) {
        g_key_file_unref(kf);
        g_clear_error(&err);
        return FALSE;
    }

    gchar *address = g_key_file_get_string(kf, CONFIG_GROUP, "address", NULL);
    gchar *auth_key = g_key_file_get_string(kf, CONFIG_GROUP, "auth_key", NULL);

    if (auth_key != NULL) {
        /* g_strcanon may return auth_key itself when no chars are stripped. */
        gchar *compact = g_strcanon(auth_key, "0123456789abcdefABCDEF", '\0');
        if (strlen(compact) == 32) {
            g_strlcpy(state->auth_key_hex, compact, sizeof(state->auth_key_hex));
        }
        g_free(compact);
        auth_key = NULL;
    }

    if (address != NULL && address[0] != '\0') {
        g_free(state->selected_address);
        state->selected_address = g_strdup(address);
    }

    g_free(state->weather_location_name);
    state->weather_location_name = g_key_file_get_string(kf, WEATHER_GROUP, "location", NULL);
    if (state->weather_location_name == NULL || state->weather_location_name[0] == '\0') {
        g_free(state->weather_location_name);
        state->weather_location_name = g_strdup("Home");
    }
    GError *werr = NULL;
    state->weather_latitude = g_key_file_get_double(kf, WEATHER_GROUP, "latitude", &werr);
    if (werr != NULL) {
        g_clear_error(&werr);
        state->weather_latitude = 52.5200;
    }
    state->weather_longitude = g_key_file_get_double(kf, WEATHER_GROUP, "longitude", &werr);
    if (werr != NULL) {
        g_clear_error(&werr);
        state->weather_longitude = 13.4050;
    }
    state->weather_coords_valid = TRUE;

    state->notifications_enabled = g_key_file_get_boolean(kf, NOTIF_GROUP, "enabled", NULL);
    state->notifications_min_interval_ms = (guint)g_key_file_get_integer(kf, NOTIF_GROUP, "min_interval_ms", NULL);
    if (state->notifications_min_interval_ms == 0) {
        state->notifications_min_interval_ms = 3000;
    }
    if (!g_key_file_has_key(kf, NOTIF_GROUP, "enabled", NULL)) {
        state->notifications_enabled = TRUE;
    }
    state->notification_next_id = 1;

    state->share_location_with_band =
        !g_key_file_has_key(kf, LOCATION_GROUP, "share_with_band", NULL) ||
        g_key_file_get_boolean(kf, LOCATION_GROUP, "share_with_band", NULL);
    g_free(state->location_unavailable_reply);
    state->location_unavailable_reply = g_key_file_get_string(kf, LOCATION_GROUP, "reply_when_unavailable", NULL);
    if (state->location_unavailable_reply == NULL) {
        state->location_unavailable_reply = g_strdup("none");
    }

    state->profile_height_cm = (guint)g_key_file_get_integer(kf, PROFILE_GROUP, "height_cm", NULL);
    state->profile_weight_kg = (float)g_key_file_get_double(kf, PROFILE_GROUP, "weight_kg", NULL);
    state->profile_birthday_yyyymmdd = (guint)g_key_file_get_integer(kf, PROFILE_GROUP, "birthday", NULL);
    if (g_key_file_has_key(kf, PROFILE_GROUP, "gender", NULL)) {
        guint g = (guint)g_key_file_get_integer(kf, PROFILE_GROUP, "gender", NULL);
        /* Legacy configs used Mi Fit wire values (2 = female). */
        if (g == 2u) {
            g = 0u;
        }
        if (g != 0u && g != 1u) {
            g = 1u;
        }
        state->profile_gender = g;
    } else {
        state->profile_gender = 1u;
    }
    state->profile_max_hr = (guint)g_key_file_get_integer(kf, PROFILE_GROUP, "max_heart_rate", NULL);
    state->profile_goal_moving = (guint)g_key_file_get_integer(kf, PROFILE_GROUP, "goal_activity_minutes", NULL);
    state->profile_cycle_length_days = (guint)g_key_file_get_integer(kf, PROFILE_GROUP, "cycle_length_days", NULL);
    state->profile_last_period_yyyymmdd = (guint)g_key_file_get_integer(kf, PROFILE_GROUP, "last_period", NULL);
    if (state->profile_goal_moving > 0) {
        state->goal_moving = state->profile_goal_moving;
    }

    g_free(address);
    g_free(auth_key);
    g_key_file_unref(kf);
    return TRUE;
}

gboolean miwear_config_save(const AppState *state, const char *path) {
    const char *cfg_path = path != NULL ? path : miwear_config_default_path();
    gchar *dir = g_path_get_dirname(cfg_path);

    if (g_mkdir_with_parents(dir, 0700) < 0) {
        g_free(dir);
        return FALSE;
    }
    g_free(dir);

    GKeyFile *kf = g_key_file_new();
    g_key_file_set_string(kf, CONFIG_GROUP, "address",
                          state->selected_address != NULL ? state->selected_address : "");
    g_key_file_set_string(kf, CONFIG_GROUP, "auth_key", state->auth_key_hex);
    if (state->weather_location_name != NULL) {
        g_key_file_set_string(kf, WEATHER_GROUP, "location", state->weather_location_name);
    }
    if (state->weather_coords_valid) {
        g_key_file_set_double(kf, WEATHER_GROUP, "latitude", state->weather_latitude);
        g_key_file_set_double(kf, WEATHER_GROUP, "longitude", state->weather_longitude);
    }
    g_key_file_set_boolean(kf, NOTIF_GROUP, "enabled", state->notifications_enabled);
    g_key_file_set_integer(kf, NOTIF_GROUP, "min_interval_ms", (gint)state->notifications_min_interval_ms);
    g_key_file_set_boolean(kf, LOCATION_GROUP, "share_with_band", state->share_location_with_band);
    if (state->location_unavailable_reply != NULL) {
        g_key_file_set_string(kf, LOCATION_GROUP, "reply_when_unavailable", state->location_unavailable_reply);
    }

    if (state->profile_height_cm > 0) {
        g_key_file_set_integer(kf, PROFILE_GROUP, "height_cm", (gint)state->profile_height_cm);
    }
    if (state->profile_weight_kg > 0.0f) {
        g_key_file_set_double(kf, PROFILE_GROUP, "weight_kg", state->profile_weight_kg);
    }
    if (state->profile_birthday_yyyymmdd > 0) {
        g_key_file_set_integer(kf, PROFILE_GROUP, "birthday", (gint)state->profile_birthday_yyyymmdd);
    }
    g_key_file_set_integer(kf, PROFILE_GROUP, "gender", (gint)state->profile_gender);
    if (state->profile_max_hr > 0) {
        g_key_file_set_integer(kf, PROFILE_GROUP, "max_heart_rate", (gint)state->profile_max_hr);
    }
    if (state->profile_goal_moving > 0) {
        g_key_file_set_integer(kf, PROFILE_GROUP, "goal_activity_minutes", (gint)state->profile_goal_moving);
    }
    if (state->profile_cycle_length_days > 0) {
        g_key_file_set_integer(kf, PROFILE_GROUP, "cycle_length_days", (gint)state->profile_cycle_length_days);
    }
    if (state->profile_last_period_yyyymmdd > 0) {
        g_key_file_set_integer(kf, PROFILE_GROUP, "last_period", (gint)state->profile_last_period_yyyymmdd);
    }

    gsize len = 0;
    gchar *data = g_key_file_to_data(kf, &len, NULL);
    gboolean ok = g_file_set_contents(cfg_path, data, (gssize)len, NULL) == TRUE;
    g_free(data);
    g_key_file_unref(kf);
    return ok;
}
