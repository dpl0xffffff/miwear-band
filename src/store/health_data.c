#include "salus/store/health_data.h"

#include "salus/store/db.h"
#include "salus/ui/dashboard.h"

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

void salus_health_aggregate_day(SalusApp *state, const gchar *day_key, SalusDayHealthAgg *out) {
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

    const SalusDailySummaryRow *summary =
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
        const SalusSleepDurationSummary *sd = g_hash_table_lookup(state->sleep_duration_by_day, day_key);
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

gboolean salus_spo2_value_valid(int spo2) {
    return spo2 > 0 && spo2 <= 100;
}

gboolean salus_stress_value_valid(int stress) {
    return stress > 0 && stress < 255;
}

const char *salus_pressure_level_label(int pressure) {
    if (!salus_stress_value_valid(pressure)) {
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

guint salus_apply_pressure_from_activity_record_json(SalusApp *state, JsonNode *decoded, uint32_t base_ts_sec) {
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
    GArray *rows = g_array_new(FALSE, FALSE, sizeof(SalusXiaomiActivityRow));

    for (guint i = 0; i < n; i++) {
        JsonNode *el = json_array_get_element(arr, i);
        if (el == NULL || !JSON_NODE_HOLDS_OBJECT(el)) {
            continue;
        }
        const int pressure = json_item_cur_pressure(json_node_get_object(el));
        if (!salus_stress_value_valid(pressure)) {
            continue;
        }
        SalusXiaomiActivityRow r = {0};
        r.timestamp_sec = base_ts_sec > 0 ? (int32_t)(base_ts_sec + (uint32_t)i * 60u) : (int32_t)(i * 60);
        r.stress = pressure;
        r.heart_rate = -1;
        r.spo2 = -1;
        r.steps = -1;
        g_array_append_val(rows, r);
    }

    if (rows->len > 0) {
        (void)salus_db_replace_xiaomi_activity_batch((const SalusXiaomiActivityRow *)rows->data, rows->len);
        recorded_merge_xiaomi_activity_rows(state, (const SalusXiaomiActivityRow *)rows->data, rows->len);
        salus_dashboard_schedule_refresh(state);
    }
    const guint count = rows->len;
    g_array_unref(rows);
    return count;
}

guint salus_standing_display_value(int standing_field) {
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

guint salus_activity_goal_default(void) {
    return 30;
}

/*
 * The band's "active time" is the number of minutes whose per-minute activity intensity
 * (activeStreng, daily activity record) is moderate (3) or higher (4 high, 5 intense).
 * Checked against the band: 10 minutes at intensity 3 → band shows 10.
 */
#define SALUS_ACTIVE_INTENSITY_MIN 3
#define SALUS_ACTIVE_INTENSITY_MAX 5

guint salus_count_activity_minutes_from_timeline(SalusApp *state, const gchar *day_key) {
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
            if (s->intensity >= SALUS_ACTIVE_INTENSITY_MIN && s->intensity <= SALUS_ACTIVE_INTENSITY_MAX) {
                count++;
            }
        }
    }
    g_mutex_unlock(&state->recorded_lock);
    return count;
}

guint salus_standing_hours_from_bitmap(int standing_raw) {
    return salus_standing_display_value(standing_raw);
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

guint salus_count_moderate_activity_minutes_json(JsonNode *decoded) {
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

float salus_profile_bmi_kg_m2(float weight_kg, guint height_cm) {
    if (weight_kg <= 0.0f || height_cm == 0) {
        return 0.0f;
    }
    const float h_m = (float)height_cm / 100.0f;
    return weight_kg / (h_m * h_m);
}

int salus_hr_zone_index(int hr_bpm, int max_hr) {
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

const char *salus_hr_zone_label(int zone) {
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

int salus_cycle_day_of_period(GDate *today, guint last_period_yyyymmdd, guint cycle_length_days) {
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

const char *salus_cycle_phase_label(int cycle_day, guint cycle_length_days) {
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

