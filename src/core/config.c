#include "salus/config.h"
#include "salus/migrate.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

static const char *LEGACY_DEVICE_GROUP = "device"; /* single-device config before Salus */
static const char *DEVICES_GROUP = "devices";
#define DEVICE_GROUP_PREFIX "device "
static const char *WEATHER_GROUP = "weather";
static const char *NOTIF_GROUP = "notifications";
static const char *LOCATION_GROUP = "location";
static const char *PROFILE_GROUP = "profile";

const char *salus_config_default_path(void) {
    /* Computed once: callers treat the result as borrowed (it was leaked on every load/save). */
    static gsize once;
    static gchar *path;
    if (g_once_init_enter(&once)) {
        path = salus_config_file_path();
        g_once_init_leave(&once, 1);
    }
    return path;
}

/* [device AA:BB:…] groups (driver, name, pairing fields) + [devices] active=…; the old single
 * [device] group (address, auth_key) becomes a Xiaomi band entry. */
static void load_devices(SalusApp *state, GKeyFile *kf) {
    gsize n = 0;
    gchar **groups = g_key_file_get_groups(kf, &n);
    for (gsize i = 0; i < n; i++) {
        if (!g_str_has_prefix(groups[i], DEVICE_GROUP_PREFIX)) {
            continue;
        }
        const char *address = groups[i] + strlen(DEVICE_GROUP_PREFIX);
        gchar *driver = g_key_file_get_string(kf, groups[i], "driver", NULL);
        SalusDeviceConfig *cfg = salus_device_config_new(address, driver != NULL ? driver : "xiaomi-band");
        g_free(driver);
        gchar **keys = g_key_file_get_keys(kf, groups[i], NULL, NULL);
        for (gchar **k = keys; k != NULL && *k != NULL; k++) {
            gchar *v = g_key_file_get_string(kf, groups[i], *k, NULL);
            if (g_strcmp0(*k, "name") == 0) {
                cfg->name = v;
                continue;
            }
            if (g_strcmp0(*k, "driver") != 0) {
                salus_device_config_set(cfg, *k, v);
            }
            g_free(v);
        }
        g_strfreev(keys);
        salus_app_save_device(state, cfg);
        salus_device_config_free(cfg);
    }
    g_strfreev(groups);

    gchar *address = g_key_file_get_string(kf, LEGACY_DEVICE_GROUP, "address", NULL);
    if (address != NULL && address[0] != '\0' && salus_app_saved_device(state, address) == NULL) {
        SalusDeviceConfig *cfg = salus_device_config_new(address, "xiaomi-band");
        gchar *key = g_key_file_get_string(kf, LEGACY_DEVICE_GROUP, "auth_key", NULL);
        if (key != NULL) {
            g_strcanon(key, "0123456789abcdefABCDEF", '\0'); /* in place */
            if (strlen(key) == 32) {
                salus_device_config_set(cfg, "auth_key", key);
            }
            g_free(key);
        }
        salus_app_save_device(state, cfg);
        salus_device_config_free(cfg);
    }

    gchar *active = g_key_file_get_string(kf, DEVICES_GROUP, "active", NULL);
    if (active == NULL || salus_app_saved_device(state, active) == NULL) {
        g_free(active);
        active = address != NULL && address[0] != '\0' ? g_strdup(address) : NULL;
    }
    if (active == NULL && state->saved_devices->len > 0) {
        active = g_strdup(((SalusDeviceConfig *)g_ptr_array_index(state->saved_devices, 0))->address);
    }
    if (active != NULL) {
        salus_app_activate_device(state, active);
    }
    g_free(active);
    g_free(address);
}

gboolean salus_config_load(SalusApp *state, const char *path) {
    const char *cfg_path = path != NULL ? path : salus_config_default_path();
    GKeyFile *kf = g_key_file_new();
    GError *err = NULL;

    if (!g_key_file_load_from_file(kf, cfg_path, G_KEY_FILE_NONE, &err)) {
        g_key_file_unref(kf);
        g_clear_error(&err);
        return FALSE;
    }

    load_devices(state, kf);

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

    g_key_file_unref(kf);
    return TRUE;
}

static void save_devices(const SalusApp *state, GKeyFile *kf) {
    gsize n = 0;
    gchar **groups = g_key_file_get_groups(kf, &n);
    for (gsize i = 0; i < n; i++) {
        if (g_str_has_prefix(groups[i], DEVICE_GROUP_PREFIX)) {
            g_key_file_remove_group(kf, groups[i], NULL);
        }
    }
    g_strfreev(groups);
    g_key_file_remove_group(kf, LEGACY_DEVICE_GROUP, NULL);
    g_key_file_remove_group(kf, DEVICES_GROUP, NULL);

    for (guint i = 0; state->saved_devices != NULL && i < state->saved_devices->len; i++) {
        const SalusDeviceConfig *cfg = g_ptr_array_index(state->saved_devices, i);
        gchar *group = g_strconcat(DEVICE_GROUP_PREFIX, cfg->address, NULL);
        g_key_file_set_string(kf, group, "driver", cfg->driver_id);
        if (cfg->name != NULL) {
            g_key_file_set_string(kf, group, "name", cfg->name);
        }
        GHashTableIter it;
        gpointer k, v;
        g_hash_table_iter_init(&it, cfg->fields);
        while (g_hash_table_iter_next(&it, &k, &v)) {
            g_key_file_set_string(kf, group, k, v);
        }
        g_free(group);
    }
    if (state->device != NULL) {
        g_key_file_set_string(kf, DEVICES_GROUP, "active", state->device->config->address);
    }
}

gboolean salus_config_save(const SalusApp *state, const char *path) {
    const char *cfg_path = path != NULL ? path : salus_config_default_path();
    gchar *dir = g_path_get_dirname(cfg_path);

    if (g_mkdir_with_parents(dir, 0700) < 0) {
        g_free(dir);
        return FALSE;
    }
    g_free(dir);

    /* Start from the file as it is: other parts of the app keep their own groups in it (the
     * weight log), which a fresh key file would drop. */
    GKeyFile *kf = g_key_file_new();
    g_key_file_load_from_file(kf, cfg_path, G_KEY_FILE_KEEP_COMMENTS, NULL);
    save_devices(state, kf);
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
