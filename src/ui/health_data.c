#include "miwear/ui/health_data.h"

#include "miwear/db/db.h"

#include <json-glib/json-glib.h>
#include <string.h>

static int sleep_stage_bucket(int stage) {
    switch (stage) {
        case 2: return 2;
        case 3: return 3;
        case 4: return 4;
        case 5: return 5;
        default: return -1;
    }
}

void miwear_health_aggregate_day(AppState *state, const gchar *day_key, MiwearDayHealthAgg *out) {
    memset(out, 0, sizeof(*out));
    out->day_key = day_key;
    out->steps = -1;
    out->calories = -1;

    if (state == NULL || day_key == NULL) {
        return;
    }

    g_mutex_lock(&state->recorded_lock);
    GArray *arr = (state->recorded_days != NULL) ? g_hash_table_lookup(state->recorded_days, day_key) : NULL;
    if (arr != NULL) {
        for (guint i = 0; i < arr->len; i++) {
            const RecordedSample *s = &g_array_index(arr, RecordedSample, i);
            if (s->steps >= 0) {
                out->steps = (out->steps < 0) ? s->steps : out->steps + s->steps;
            }
            if (s->active_calories >= 0) { /* per-minute activityCalories; the day's sum = totalactiveCalories */
                out->calories = (out->calories < 0) ? s->active_calories : out->calories + s->active_calories;
            }
            if (s->heart_rate > 0 && s->heart_rate < 240) {
                out->hr_sum += s->heart_rate;
                out->hr_n++;
                if (s->heart_rate > out->hr_max) {
                    out->hr_max = s->heart_rate;
                }
            }
            if (s->spo2 > 0 && s->spo2 <= 100) {
                out->spo2_sum += s->spo2;
                out->spo2_n++;
            }
            if (s->stress > 0 && s->stress < 255) {
                out->stress_sum += s->stress;
                out->stress_n++;
            }
            if (s->sleep_stage >= 0) {
                out->has_sleep = TRUE;
                switch (sleep_stage_bucket(s->sleep_stage)) {
                    case 2: out->sleep_deep++; break;
                    case 3: out->sleep_light++; break;
                    case 4: out->sleep_rem++; break;
                    case 5: out->sleep_awake++; break;
                    default: out->sleep_other++; break;
                }
            }
        }
    }

    const MiwearDailySummaryRow *summary =
        state->daily_summaries != NULL ? g_hash_table_lookup(state->daily_summaries, day_key) : NULL;
    if (summary != NULL) {
        out->has_summary = TRUE;
        out->summary = *summary;
        /* The band's totals include minutes not (yet) present as per-minute samples. */
        if (summary->steps > 0) {
            out->steps = summary->steps;
        }
        /* The band shows totalactiveCalories, not totalCalories. Both it and the per-minute sum are
         * synced values that only grow during the day; the larger one is the more recent. */
        if (summary->has_active_calories && summary->active_calories > out->calories) {
            out->calories = summary->active_calories;
        }
    }
    if (state->sleep_duration_by_day != NULL) {
        const MiwearSleepDurationSummary *sd = g_hash_table_lookup(state->sleep_duration_by_day, day_key);
        if (sd != NULL && sd->from_db) {
            if (sd->total_asleep > 0) {
                out->sleep_total_band = sd->total_asleep;
                out->has_sleep = TRUE;
            }
            const int db_stages = sd->deep + sd->light + sd->rem + sd->awake;
            const gboolean total_only_fallback =
                sd->total_asleep > 0 && sd->deep == 0 && sd->rem == 0 && sd->awake == 0 &&
                sd->light == sd->total_asleep;
            if (db_stages > 0 && !total_only_fallback) {
                out->sleep_deep = sd->deep;
                out->sleep_light = sd->light;
                out->sleep_rem = sd->rem;
                out->sleep_awake = sd->awake;
                out->sleep_other = 0;
                out->has_sleep = TRUE;
            }
        }
    }
    g_mutex_unlock(&state->recorded_lock);
}

int miwear_health_sleep_total_minutes(const MiwearDayHealthAgg *agg) {
    if (agg == NULL || !agg->has_sleep) {
        return 0;
    }
    /* Mi Fitness / band “sleep time” = asleep (deep+light+REM), not awake-in-bed minutes. */
    if (agg->sleep_total_band > 0) {
        return agg->sleep_total_band;
    }
    const int asleep = agg->sleep_deep + agg->sleep_light + agg->sleep_rem + agg->sleep_other;
    if (asleep > 0) {
        return asleep;
    }
    return agg->sleep_deep + agg->sleep_light + agg->sleep_rem + agg->sleep_awake + agg->sleep_other;
}

gboolean miwear_spo2_value_valid(int spo2) {
    return spo2 > 0 && spo2 <= 100;
}

gboolean miwear_stress_value_valid(int stress) {
    return stress > 0 && stress < 255;
}

int miwear_latest_stress_for_day(AppState *state, const gchar *day_key) {
    if (state == NULL || day_key == NULL) {
        return -1;
    }
    int latest = -1;
    guint latest_minute = 0;
    gboolean have = FALSE;
    g_mutex_lock(&state->recorded_lock);
    GArray *arr =
        (state->recorded_days != NULL) ? g_hash_table_lookup(state->recorded_days, day_key) : NULL;
    if (arr != NULL) {
        for (guint i = 0; i < arr->len; i++) {
            const RecordedSample *s = &g_array_index(arr, RecordedSample, i);
            if (!miwear_stress_value_valid(s->stress)) {
                continue;
            }
            if (!have || s->minute_of_day >= latest_minute) {
                latest_minute = s->minute_of_day;
                latest = s->stress;
                have = TRUE;
            }
        }
    }
    g_mutex_unlock(&state->recorded_lock);
    return have ? latest : -1;
}

static gboolean day_key_unix_range(const gchar *day_key, int32_t *start_out, int32_t *end_out) {
    if (day_key == NULL || start_out == NULL || end_out == NULL) {
        return FALSE;
    }
    int y = 0, m = 0, d = 0;
    if (sscanf(day_key, "%d-%d-%d", &y, &m, &d) != 3 || y < 1970) {
        return FALSE;
    }
    GDateTime *start = g_date_time_new_local(y, m, d, 0, 0, 0.0);
    GDateTime *end = g_date_time_new_local(y, m, d, 23, 59, 59.0);
    if (start == NULL || end == NULL) {
        if (start != NULL) {
            g_date_time_unref(start);
        }
        if (end != NULL) {
            g_date_time_unref(end);
        }
        return FALSE;
    }
    *start_out = (int32_t)g_date_time_to_unix(start);
    *end_out = (int32_t)g_date_time_to_unix(end);
    g_date_time_unref(start);
    g_date_time_unref(end);
    return TRUE;
}

int miwear_display_stress_for_day(AppState *state, const gchar *day_key, const char **source_out) {
    if (source_out != NULL) {
        *source_out = NULL;
    }
    int32_t day_start = 0;
    int32_t day_end = 0;
    const gboolean have_range = day_key_unix_range(day_key, &day_start, &day_end);

    int best = -1;
    int32_t best_ts = 0;
    const char *best_src = NULL;

    if (state != NULL && state->live_stress_text != NULL && state->live_stress_text[0] != '\0') {
        const int live = atoi(state->live_stress_text);
        if (miwear_stress_value_valid(live)) {
            const int32_t ts = state->live_stress_ts_sec;
            if (ts == 0 || (have_range && ts >= day_start && ts <= day_end)) {
                best = live;
                best_ts = ts > 0 ? ts : best_ts;
                best_src = "live-cache";
            }
        }
    }

    if (have_range) {
        int db_stress = -1;
        int32_t db_ts = 0;
        if (miwear_db_latest_stress_in_range(day_start, day_end, &db_stress, &db_ts) &&
            miwear_stress_value_valid(db_stress) && db_ts >= best_ts) {
            best = db_stress;
            best_ts = db_ts;
            best_src = "activity-db";
        }
    }

    const int timeline = miwear_latest_stress_for_day(state, day_key);
    if (miwear_stress_value_valid(timeline) && best_ts == 0) {
        best = timeline;
        best_src = "timeline-minute";
    }

    if (source_out != NULL) {
        *source_out = best_src;
    }
    return best;
}

const char *miwear_pressure_level_label(int pressure) {
    if (!miwear_stress_value_valid(pressure)) {
        return "—";
    }
    if (pressure <= 25) {
        return "Relaxed";
    }
    if (pressure <= 50) {
        return "Mild";
    }
    if (pressure <= 75) {
        return "Moderate";
    }
    return "High";
}

static int json_item_cur_pressure(JsonObject *item) {
    if (item == NULL || !json_object_has_member(item, "curPressure")) {
        return -1;
    }
    JsonObject *block = json_object_get_object_member(item, "curPressure");
    if (block == NULL || !json_object_has_member(block, "curPressure")) {
        return -1;
    }
    return json_object_get_int_member(block, "curPressure");
}

guint miwear_apply_pressure_from_activity_record_json(AppState *state, JsonNode *decoded, uint32_t base_ts_sec) {
    if (state == NULL || decoded == NULL || !JSON_NODE_HOLDS_OBJECT(decoded)) {
        return 0;
    }
    JsonObject *root = json_node_get_object(decoded);
    if (!json_object_has_member(root, "body")) {
        return 0;
    }
    JsonNode *body_node = json_object_get_member(root, "body");
    if (body_node == NULL || !JSON_NODE_HOLDS_ARRAY(body_node)) {
        return 0;
    }
    JsonArray *arr = json_node_get_array(body_node);
    const guint n = json_array_get_length(arr);
    GArray *rows = g_array_new(FALSE, FALSE, sizeof(MiwearXiaomiActivityRow));
    int latest = -1;
    int32_t latest_ts = 0;

    for (guint i = 0; i < n; i++) {
        JsonNode *el = json_array_get_element(arr, i);
        if (el == NULL || !JSON_NODE_HOLDS_OBJECT(el)) {
            continue;
        }
        const int pressure = json_item_cur_pressure(json_node_get_object(el));
        if (!miwear_stress_value_valid(pressure)) {
            continue;
        }
        MiwearXiaomiActivityRow r = {0};
        r.timestamp_sec = base_ts_sec > 0 ? (int32_t)(base_ts_sec + (uint32_t)i * 60u) : (int32_t)(i * 60);
        r.stress = pressure;
        r.heart_rate = -1;
        r.spo2 = -1;
        r.steps = -1;
        g_array_append_val(rows, r);
        if (r.timestamp_sec >= latest_ts) {
            latest_ts = r.timestamp_sec;
            latest = pressure;
        }
    }

    if (rows->len > 0) {
        (void)miwear_db_replace_xiaomi_activity_batch((const MiwearXiaomiActivityRow *)rows->data, rows->len);
        recorded_merge_xiaomi_activity_rows(state, (const MiwearXiaomiActivityRow *)rows->data, rows->len);
        if (latest >= 0) {
            gchar *txt = g_strdup_printf("%d", latest);
            schedule_metrics_update(
                state,
                FALSE, 0,
                FALSE, 0,
                FALSE, 0,
                FALSE, 0,
                FALSE, 0,
                FALSE, 0,
                NULL, txt);
            g_free(txt);
        }
    }
    const guint count = rows->len;
    g_array_unref(rows);
    return count;
}

guint miwear_standing_display_value(int standing_field) {
    if (standing_field <= 0) {
        return 0;
    }
    /* Realtime stats and newer bands expose a counter (e.g. 57), not a 24-bit hour map. */
    if (standing_field > 24) {
        return (guint)standing_field;
    }
    guint hours = 0;
    for (int i = 0; i < 24; i++) {
        if ((standing_field >> i) & 1) {
            hours++;
        }
    }
    return hours > 0 ? hours : (guint)standing_field;
}

void miwear_format_standing_value(guint value, gchar *buf, gsize buf_len) {
    if (buf == NULL || buf_len == 0) {
        return;
    }
    if (value > 24) {
        g_snprintf(buf, (gint)buf_len, "%u", value);
    } else {
        g_snprintf(buf, (gint)buf_len, "%u h", value);
    }
}

guint miwear_activity_goal_default(void) {
    return 30;
}

/*
 * The band's "active time" is the number of minutes whose per-minute activity intensity
 * (activeStreng, daily activity record) is moderate (3) or higher (4 high, 5 intense).
 * Checked against the band: 10 minutes at intensity 3 → band shows 10.
 */
#define MIWEAR_ACTIVE_INTENSITY_MIN 3
#define MIWEAR_ACTIVE_INTENSITY_MAX 5

guint miwear_count_activity_minutes_from_timeline(AppState *state, const gchar *day_key) {
    if (state == NULL || day_key == NULL) {
        return 0;
    }
    guint count = 0;
    g_mutex_lock(&state->recorded_lock);
    GArray *arr =
        (state->recorded_days != NULL) ? g_hash_table_lookup(state->recorded_days, day_key) : NULL;
    if (arr != NULL) {
        for (guint i = 0; i < arr->len; i++) {
            const RecordedSample *s = &g_array_index(arr, RecordedSample, i);
            if (s->intensity >= MIWEAR_ACTIVE_INTENSITY_MIN && s->intensity <= MIWEAR_ACTIVE_INTENSITY_MAX) {
                count++;
            }
        }
    }
    g_mutex_unlock(&state->recorded_lock);
    return count;
}

guint miwear_standing_hours_from_bitmap(int standing_raw) {
    return miwear_standing_display_value(standing_raw);
}

static int json_item_active_strength(JsonObject *item) {
    if (item == NULL || !json_object_has_member(item, "activeStrengthAndSportType")) {
        return -1;
    }
    JsonObject *strength_obj = json_object_get_object_member(item, "activeStrengthAndSportType");
    if (strength_obj == NULL || !json_object_has_member(strength_obj, "activeStreng")) {
        return -1;
    }
    return json_object_get_int_member(strength_obj, "activeStreng");
}

guint miwear_count_moderate_activity_minutes_json(JsonNode *decoded) {
    if (decoded == NULL || !JSON_NODE_HOLDS_OBJECT(decoded)) {
        return 0;
    }
    JsonObject *root = json_node_get_object(decoded);
    if (json_object_has_member(root, "body")) {
        JsonNode *body_node = json_object_get_member(root, "body");
        if (body_node == NULL || !JSON_NODE_HOLDS_ARRAY(body_node)) {
            return 0;
        }
        JsonArray *arr = json_node_get_array(body_node);
        guint count = 0;
        const guint n = json_array_get_length(arr);
        for (guint i = 0; i < n; i++) {
            JsonNode *el = json_array_get_element(arr, i);
            if (el == NULL || !JSON_NODE_HOLDS_OBJECT(el)) {
                continue;
            }
            const int strength = json_item_active_strength(json_node_get_object(el));
            /* Schema: 3=moderate, 4=high, 5=very high — matches Mi Fitness activity-time ring. */
            if (strength >= 3 && strength <= 5) {
                count++;
            }
        }
        return count;
    }
    return 0;
}

float miwear_profile_bmi_kg_m2(float weight_kg, guint height_cm) {
    if (weight_kg <= 0.0f || height_cm == 0) {
        return 0.0f;
    }
    const float h_m = (float)height_cm / 100.0f;
    return weight_kg / (h_m * h_m);
}

int miwear_hr_zone_index(int hr_bpm, int max_hr) {
    if (hr_bpm <= 0 || max_hr <= 0) {
        return 0;
    }
    const int pct = (hr_bpm * 100) / max_hr;
    if (pct < 60) {
        return 1;
    }
    if (pct < 70) {
        return 2;
    }
    if (pct < 80) {
        return 3;
    }
    if (pct < 90) {
        return 4;
    }
    return 5;
}

const char *miwear_hr_zone_label(int zone) {
    switch (zone) {
    case 1: return "Warm-up";
    case 2: return "Fat burn";
    case 3: return "Aerobic";
    case 4: return "Anaerobic";
    case 5: return "Peak";
    default: return "";
    }
}

static GDate *date_from_yyyymmdd(guint yyyymmdd) {
    const guint y = yyyymmdd / 10000u;
    const guint m = (yyyymmdd / 100u) % 100u;
    const guint d = yyyymmdd % 100u;
    if (y < 1970 || m < 1 || m > 12 || d < 1 || d > 31) {
        return NULL;
    }
    return g_date_new_dmy(d, m, y);
}

int miwear_cycle_day_of_period(GDate *today, guint last_period_yyyymmdd, guint cycle_length_days) {
    if (today == NULL || last_period_yyyymmdd == 0 || cycle_length_days < 20 || cycle_length_days > 45) {
        return 0;
    }
    GDate *start = date_from_yyyymmdd(last_period_yyyymmdd);
    if (start == NULL) {
        return 0;
    }
    if (g_date_compare(today, start) < 0) {
        g_date_free(start);
        return 0;
    }
    const guint days = g_date_days_between(start, today);
    g_date_free(start);
    return (int)(days % cycle_length_days) + 1;
}

const char *miwear_cycle_phase_label(int cycle_day, guint cycle_length_days) {
    if (cycle_day <= 0 || cycle_length_days == 0) {
        return "Set cycle in config";
    }
    if (cycle_day <= 5) {
        return "Menstrual";
    }
    if (cycle_day <= 13) {
        return "Follicular";
    }
    if (cycle_day <= 16) {
        return "Ovulation window";
    }
    return "Luteal";
}

void miwear_format_activity_minutes(guint minutes, guint goal_minutes, gchar *buf, gsize buf_len) {
    if (buf == NULL || buf_len == 0) {
        return;
    }
    if (goal_minutes == 0) {
        goal_minutes = miwear_activity_goal_default();
    }
    g_snprintf(buf, (gint)buf_len, "%u/%u", minutes, goal_minutes);
}

gboolean miwear_health_sleep_has_stage_breakdown(const MiwearDayHealthAgg *agg) {
    if (agg == NULL || !agg->has_sleep) {
        return FALSE;
    }
    return agg->sleep_other == 0;
}
