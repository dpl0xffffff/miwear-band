#include "miwear/weight_log.h"
#include "miwear/config.h"

#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>

static const char *WEIGHT_GROUP = "weight";

static guint today_yyyymmdd(void) {
    GDateTime *now = g_date_time_new_now_local();
    const guint y = (guint)g_date_time_get_year(now);
    const guint m = (guint)g_date_time_get_month(now);
    const guint d = (guint)g_date_time_get_day_of_month(now);
    g_date_time_unref(now);
    return y * 10000u + m * 100u + d;
}

static gint weight_entry_newest_first(gconstpointer a, gconstpointer b) {
    const MiwearWeightEntry *ea = a;
    const MiwearWeightEntry *eb = b;
    if (ea->date_yyyymmdd > eb->date_yyyymmdd) {
        return -1;
    }
    if (ea->date_yyyymmdd < eb->date_yyyymmdd) {
        return 1;
    }
    return 0;
}

static gboolean parse_entry(const char *token, MiwearWeightEntry *out) {
    if (token == NULL || out == NULL) {
        return FALSE;
    }
    char *colon = strchr(token, ':');
    if (colon == NULL) {
        return FALSE;
    }
    *colon = '\0';
    const guint date = (guint)strtoul(token, NULL, 10);
    const float kg = (float)g_ascii_strtod(colon + 1, NULL);
    *colon = ':';
    if (date < 20000101u || kg <= 0.0f || kg > 500.0f) {
        return FALSE;
    }
    out->date_yyyymmdd = date;
    out->kg = kg;
    return TRUE;
}

static gchar *serialize_log(GArray *entries) {
    GString *s = g_string_new(NULL);
    for (guint i = 0; i < entries->len; i++) {
        const MiwearWeightEntry *e = &g_array_index(entries, MiwearWeightEntry, i);
        if (s->len > 0) {
            g_string_append_c(s, ',');
        }
        g_string_append_printf(s, "%u:%.2f", e->date_yyyymmdd, e->kg);
    }
    return g_string_free(s, FALSE);
}

static gboolean persist_log(const AppState *state) {
    if (state == NULL || state->weight_log == NULL) {
        return FALSE;
    }
    const char *cfg_path = miwear_config_default_path();
    GKeyFile *kf = g_key_file_new();
    GError *err = NULL;
    g_key_file_load_from_file(kf, cfg_path, G_KEY_FILE_NONE, &err);
    g_clear_error(&err);

    if (state->weight_log->len > 0) {
        const MiwearWeightEntry *latest = &g_array_index(state->weight_log, MiwearWeightEntry, 0);
        g_key_file_set_double(kf, WEIGHT_GROUP, "current_kg", latest->kg);
        g_key_file_set_integer(kf, WEIGHT_GROUP, "current_date", (gint)latest->date_yyyymmdd);
        gchar *log = serialize_log(state->weight_log);
        g_key_file_set_string(kf, WEIGHT_GROUP, "log", log);
        g_free(log);
    } else {
        g_key_file_remove_key(kf, WEIGHT_GROUP, "current_kg", NULL);
        g_key_file_remove_key(kf, WEIGHT_GROUP, "current_date", NULL);
        g_key_file_remove_key(kf, WEIGHT_GROUP, "log", NULL);
    }

    gsize len = 0;
    gchar *data = g_key_file_to_data(kf, &len, NULL);
    g_key_file_unref(kf);
    if (data == NULL) {
        return FALSE;
    }
    gchar *dir = g_path_get_dirname(cfg_path);
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);
    const gboolean ok = g_file_set_contents(cfg_path, data, (gssize)len, NULL);
    g_free(data);
    return ok;
}

void miwear_weight_log_load(AppState *state) {
    if (state == NULL) {
        return;
    }
    if (state->weight_log == NULL) {
        state->weight_log = g_array_new(FALSE, FALSE, sizeof(MiwearWeightEntry));
    } else {
        g_array_set_size(state->weight_log, 0);
    }

    const char *cfg_path = miwear_config_default_path();
    GKeyFile *kf = g_key_file_new();
    GError *err = NULL;
    if (!g_key_file_load_from_file(kf, cfg_path, G_KEY_FILE_NONE, &err)) {
        g_key_file_unref(kf);
        g_clear_error(&err);
        state->weight_log_loaded = TRUE;
        return;
    }

    const gchar *log_str = g_key_file_get_string(kf, WEIGHT_GROUP, "log", NULL);
    if (log_str != NULL && log_str[0] != '\0') {
        gchar **tokens = g_strsplit(log_str, ",", -1);
        for (gchar **p = tokens; p != NULL && *p != NULL; p++) {
            MiwearWeightEntry e = {0};
            if (parse_entry(*p, &e)) {
                g_array_append_val(state->weight_log, e);
            }
        }
        g_strfreev(tokens);
    }
    if (state->weight_log->len > 1) {
        g_array_sort(state->weight_log, weight_entry_newest_first);
    }

    g_key_file_unref(kf);
    state->weight_log_loaded = TRUE;
}

gboolean miwear_weight_log_has_entries(const AppState *state) {
    return state != NULL && state->weight_log != NULL && state->weight_log->len > 0;
}

float miwear_weight_log_latest_kg(const AppState *state, guint *date_yyyymmdd_out) {
    if (!miwear_weight_log_has_entries(state)) {
        if (date_yyyymmdd_out != NULL) {
            *date_yyyymmdd_out = 0;
        }
        return 0.0f;
    }
    const MiwearWeightEntry *e = &g_array_index(state->weight_log, MiwearWeightEntry, 0);
    if (date_yyyymmdd_out != NULL) {
        *date_yyyymmdd_out = e->date_yyyymmdd;
    }
    return e->kg;
}

float miwear_weight_log_band_user_kg(const AppState *state) {
    const float logged = miwear_weight_log_latest_kg(state, NULL);
    if (logged > 0.0f) {
        return logged;
    }
    if (state != NULL && state->profile_weight_kg > 0.0f) {
        return state->profile_weight_kg;
    }
    return 70.0f;
}

gboolean miwear_weight_log_add_today(AppState *state, float kg) {
    if (state == NULL || kg <= 0.0f || kg > 500.0f) {
        return FALSE;
    }
    if (state->weight_log == NULL) {
        state->weight_log = g_array_new(FALSE, FALSE, sizeof(MiwearWeightEntry));
    }

    const guint today = today_yyyymmdd();
    MiwearWeightEntry entry = {.date_yyyymmdd = today, .kg = kg};

    for (guint i = 0; i < state->weight_log->len; i++) {
        if (g_array_index(state->weight_log, MiwearWeightEntry, i).date_yyyymmdd == today) {
            g_array_remove_index(state->weight_log, i);
            break;
        }
    }
    g_array_prepend_val(state->weight_log, entry);

    /* Keep newest-first; cap history. */
    while (state->weight_log->len > 120) {
        g_array_remove_index(state->weight_log, state->weight_log->len - 1);
    }

    return persist_log(state);
}
