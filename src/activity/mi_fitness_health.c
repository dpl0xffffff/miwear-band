#include "miwear/activity/mi_fitness_health.h"
#include "miwear/activity/mi_fitness_sleep_stages.h"
#include "miwear/activity/mi_fitness_data.h"
#include "miwear/activity/mi_fitness_oneden.h"
#include "miwear/activity/manual.h"
#include "miwear/app.h"
#include "miwear/db/db.h"
#include "miwear/ui/health_data.h"
#include "miwear/ui/dashboard.h"

#include <string.h>

static int json_obj_int64(JsonObject *obj, const char *key, int64_t def) {
    if (obj == NULL || !json_object_has_member(obj, key)) {
        return (int)def;
    }
    return (int)json_object_get_int_member(obj, key);
}

static JsonObject *decoded_root_object(JsonNode *decoded) {
    if (decoded == NULL || !JSON_NODE_HOLDS_OBJECT(decoded)) {
        return NULL;
    }
    return json_node_get_object(decoded);
}

static JsonObject *decoded_body(JsonNode *decoded) {
    JsonObject *root = decoded_root_object(decoded);
    if (root == NULL) {
        return NULL;
    }
    if (!json_object_has_member(root, "body")) {
        return root;
    }
    JsonNode *body = json_object_get_member(root, "body");
    if (body == NULL || !JSON_NODE_HOLDS_OBJECT(body)) {
        return NULL;
    }
    return json_node_get_object(body);
}

static JsonObject *decoded_header(JsonNode *decoded) {
    JsonObject *root = decoded_root_object(decoded);
    if (root == NULL || !json_object_has_member(root, "header")) {
        return NULL;
    }
    JsonNode *hdr = json_object_get_member(root, "header");
    if (hdr == NULL || !JSON_NODE_HOLDS_OBJECT(hdr)) {
        return NULL;
    }
    return json_node_get_object(hdr);
}

static int json_optional_int(JsonObject *obj, const char *key) {
    if (obj == NULL || !json_object_has_member(obj, key)) {
        return -1;
    }
    return (int)json_object_get_int_member(obj, key);
}

static void apply_daily_activity_record_schema(AppState *state, JsonNode *decoded, uint32_t ts) {
    const guint pressure_points = miwear_apply_pressure_from_activity_record_json(state, decoded, ts);
    const guint moderate_min = miwear_count_moderate_activity_minutes_json(decoded);
    if (moderate_min > 0) {
        if (!state->has_activity_minutes_today || moderate_min > state->activity_minutes_today) {
            state->activity_minutes_today = moderate_min;
            state->has_activity_minutes_today = TRUE;
        }
        miwear_dashboard_schedule_refresh(state);
    }
    gchar *line = g_strdup_printf(
        "Daily activity record (schema): moderate+ minutes=%u pressure points=%u",
        moderate_min,
        pressure_points);
    append_log(state, line);
    g_free(line);
    if (pressure_points > 0) {
        miwear_dashboard_schedule_refresh(state);
    }
}

static void apply_daily_activity_report_schema(AppState *state, JsonNode *decoded) {
    JsonObject *hdr = decoded_header(decoded);
    JsonObject *body = decoded_body(decoded);
    if (body == NULL) {
        return;
    }

    const uint32_t ts =
        hdr != NULL ? (uint32_t)json_obj_int64(hdr, "timestamp", 0) : 0;
    const int tz = hdr != NULL ? json_obj_int64(hdr, "timezone", 0) : 0;

    MiwearDailySummaryRow dr = {0};
    dr.timestamp_ms = (int64_t)ts * 1000LL;
    dr.timezone = tz;
    dr.steps = json_obj_int64(body, "totalSteps", 0);
    dr.calories = json_obj_int64(body, "totalCalories", 0);
    if (dr.calories <= 0) {
        dr.calories = json_obj_int64(body, "totalactiveCalories", 0);
    }
    dr.hr_resting = json_obj_int64(body, "restingHeartRate", 0);
    dr.hr_max = json_obj_int64(body, "maxHeartRate", 0);
    dr.hr_max_ts_sec = (int32_t)json_obj_int64(body, "maxHeartRateTime", 0);
    dr.hr_min = json_obj_int64(body, "minHeartRate", 0);
    dr.hr_min_ts_sec = (int32_t)json_obj_int64(body, "minHeartRateTime", 0);
    dr.hr_avg = json_obj_int64(body, "avgHeartRate", 0);

    const int stress_avg = json_optional_int(body, "avgStress");
    const int stress_max = json_optional_int(body, "maxStress");
    const int stress_min = json_optional_int(body, "minStress");
    dr.stress_avg = miwear_stress_value_valid(stress_avg) ? stress_avg : 0;
    dr.stress_max = miwear_stress_value_valid(stress_max) ? stress_max : 0;
    dr.stress_min = miwear_stress_value_valid(stress_min) ? stress_min : 0;

    dr.standing_bits = json_obj_int64(body, "standingindicator", 0);

    const int spo2_avg = json_optional_int(body, "avgOxygenSaturation");
    const int spo2_max = json_optional_int(body, "maxOxygenSaturation");
    const int spo2_min = json_optional_int(body, "minOxygenSaturation");
    const int spo2_now = json_optional_int(body, "oxygenSaturation");
    dr.spo2_avg = miwear_spo2_value_valid(spo2_avg) ? spo2_avg
                  : miwear_spo2_value_valid(spo2_now) ? spo2_now
                                                      : 0;
    dr.spo2_max = miwear_spo2_value_valid(spo2_max) ? spo2_max : 0;
    dr.spo2_min = miwear_spo2_value_valid(spo2_min) ? spo2_min : 0;
    dr.spo2_max_ts_sec = (int32_t)json_obj_int64(body, "maxOxygenSaturationTime", 0);
    dr.spo2_min_ts_sec = (int32_t)json_obj_int64(body, "minOxygenSaturationTime", 0);

    dr.training_load_day = json_obj_int64(body, "totaltrainingLoad", 0);
    dr.training_load_week = json_obj_int64(body, "totalPast7daystrainingLoad", 0);
    dr.training_load_level = json_obj_int64(body, "trainingLoadLevel", 0);
    dr.vitality_increase_light = json_obj_int64(body, "lowIntensityearnedPoints", 0);
    dr.vitality_increase_moderate = json_obj_int64(body, "mediumIntensityearnedPoints", 0);
    dr.vitality_increase_high = json_obj_int64(body, "maxIntensityearnedPoints", 0);
    dr.vitality_current = json_obj_int64(body, "totalPast7dayearnedPoints", 0);
    dr.has_training_vitality = dr.vitality_current > 0 || dr.training_load_day > 0;

    (void)miwear_db_replace_daily_summary(&dr);
    health_apply_daily_summary(state, &dr);

    gchar *line = g_strdup_printf(
        "Daily report (schema): steps=%d standing_hours=%u stress=%d spo2=%d",
        dr.steps,
        miwear_standing_hours_from_bitmap(dr.standing_bits),
        dr.stress_avg,
        dr.spo2_avg);
    append_log(state, line);
    g_free(line);
}

static guint16 minute_of_day_local(gint64 unix_sec) {
    GDateTime *dt = g_date_time_new_from_unix_local(unix_sec);
    if (dt == NULL) {
        return 0;
    }
    const guint16 mod = (guint16)(g_date_time_get_hour(dt) * 60 + g_date_time_get_minute(dt));
    g_date_time_unref(dt);
    return mod;
}

static void append_sleep_minute(GHashTable *by_day, gint64 unix_sec, int stage) {
    GDateTime *dt = g_date_time_new_from_unix_local(unix_sec);
    if (dt == NULL) {
        return;
    }
    gchar *day_key = g_date_time_format(dt, "%Y-%m-%d");
    g_date_time_unref(dt);

    GArray *bucket = g_hash_table_lookup(by_day, day_key);
    if (bucket == NULL) {
        bucket = g_array_new(FALSE, FALSE, sizeof(RecordedSample));
        g_hash_table_insert(by_day, day_key, bucket);
    } else {
        g_free(day_key);
    }

    RecordedSample s = {0};
    s.minute_of_day = minute_of_day_local(unix_sec);
    s.steps = s.active_calories = s.distance_cm = s.heart_rate = s.energy = s.spo2 = s.stress = -1;
    s.sleep_stage = stage;
    s.sleep_source = 1;
    g_array_append_val(bucket, s);
}

static void flush_sleep_buckets(AppState *state, GHashTable *by_day) {
    GHashTableIter it;
    gpointer k, v;
    g_hash_table_iter_init(&it, by_day);
    while (g_hash_table_iter_next(&it, &k, &v)) {
        GArray *bucket = v;
        if (bucket != NULL && bucket->len > 0) {
            recorded_store_samples_for_day_key(state, (const gchar *)k,
                                               (const RecordedSample *)bucket->data, bucket->len);
        }
    }
}

static void apply_sleep_times_to_chart(AppState *state, uint32_t bed_sec, uint32_t wake_sec, int stage) {
    if (state == NULL || bed_sec == 0 || wake_sec == 0 || wake_sec <= bed_sec) {
        return;
    }
    GHashTable *by_day = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_array_unref);
    for (uint32_t sec = bed_sec; sec < wake_sec; sec += 60u) {
        append_sleep_minute(by_day, (gint64)sec, stage);
    }
    flush_sleep_buckets(state, by_day);
    g_hash_table_unref(by_day);
}

static void apply_full_day_sleep_schema(AppState *state,
                                        JsonObject *body,
                                        const uint8_t *payload,
                                        size_t payload_len) {
    if (body == NULL) {
        return;
    }
    const uint32_t go_bed = (uint32_t)json_obj_int64(body, "goBedTime", 0);
    const uint32_t start = (uint32_t)json_obj_int64(body, "startTime", 0);
    const uint32_t end = (uint32_t)json_obj_int64(body, "endTime", 0);
    const uint32_t leave = (uint32_t)json_obj_int64(body, "liveBedTime", 0);
    const uint32_t bed = go_bed != 0 ? go_bed : start;
    const uint32_t wake = leave != 0 ? leave : end;
    if (bed != 0 && wake != 0 && wake > bed) {
        apply_sleep_times_to_chart(state, bed, wake, 3);
        gchar *line = g_strdup_printf(
            "Sleep (schema): bed=%u wake=%u — timeline merged into Sleep tab",
            bed,
            wake);
        append_log(state, line);
        g_free(line);
    }
    (void)payload;
    (void)payload_len;
}

static void log_abnormal_schema(AppState *state, JsonObject *body) {
    if (body == NULL) {
        return;
    }
    gchar *line = g_strdup_printf(
        "Abnormal health event (schema): start=%d end=%d type=%d value=%d count=%d",
        json_obj_int64(body, "startTime", 0),
        json_obj_int64(body, "endTime", 0),
        json_obj_int64(body, "type", 0),
        json_obj_int64(body, "setValue", 0),
        json_obj_int64(body, "recordCount", 0));
    append_log(state, line);
    g_free(line);
}

void mi_fitness_apply_schema_health(AppState *state,
                                    uint32_t ts,
                                    int apk_daily_type,
                                    int detail,
                                    JsonNode *decoded,
                                    const uint8_t *payload,
                                    size_t payload_len) {
    (void)detail;
    if (state == NULL || decoded == NULL) {
        return;
    }
    JsonObject *body = decoded_body(decoded);
    if (apk_daily_type == 0 && detail == 0) {
        apply_daily_activity_record_schema(state, decoded, ts);
        return;
    }
    if (apk_daily_type == 0 && detail == 1) {
        apply_daily_activity_report_schema(state, decoded);
        return;
    }
    switch (apk_daily_type) {
        case 8:
            apply_full_day_sleep_schema(state, body, payload, payload_len);
            break;
        case 9:
            log_abnormal_schema(state, body);
            break;
        case 10:
            if (body != NULL && json_object_has_member(body, "records")) {
                JsonArray *arr = json_object_get_array_member(body, "records");
                const guint n = arr != NULL ? json_array_get_length(arr) : 0;
                gchar *line = g_strdup_printf("Weight sync (schema): %u record(s) decoded", n);
                append_log(state, line);
                g_free(line);
            }
            break;
        case 11:
            append_log(state, "ECG measurement decoded (schema).");
            break;
        case 12:
            if (body != NULL) {
                gchar *line = g_strdup_printf(
                    "Temperature (schema): value=%d",
                    json_obj_int64(body, "temperature", 0));
                append_log(state, line);
                g_free(line);
            }
            break;
        case 6:
            miwear_apply_manual_measurement_json(state, ts, decoded);
            break;
        default:
            break;
    }
}

static gboolean native_split(const uint8_t *payload,
                             size_t payload_len,
                             int wire_daily_type,
                             int detail,
                             int version,
                             MiwearFitnessBinarySlice *slice,
                             MiwearFitnessDataId *id_out) {
    const size_t wire_len = payload_len >= 4 ? payload_len - 4 : payload_len;
    if (!mi_fitness_data_id_from_payload(payload, wire_len, detail, id_out)) {
        return FALSE;
    }
    id_out->version = (uint8_t)version;
    id_out->daily_type = (uint8_t)wire_daily_type;
    id_out->file_type = (uint8_t)detail;
    id_out->data_type = 0;
    return mi_fitness_split_native_payload(payload, wire_len, id_out, slice);
}

static void log_field_values(AppState *state, const char *tag, const MiFitnessFieldValue *values, size_t n) {
    GString *s = g_string_new(tag);
    g_string_append(s, ":");
    for (size_t i = 0; i < n; i++) {
        if (!values[i].valid) {
            continue;
        }
        if (values[i].is_float) {
            g_string_append_printf(s, " t%d=%.2f", values[i].type, values[i].f32);
        } else {
            g_string_append_printf(s, " t%d=%lld", values[i].type, (long long)values[i].i64);
        }
    }
    append_log(state, s->str);
    g_string_free(s, TRUE);
}

gboolean mi_fitness_apply_native_health(AppState *state,
                                        uint32_t ts,
                                        int wire_daily_type,
                                        int detail,
                                        int version,
                                        const uint8_t *payload,
                                        size_t payload_len) {
    if (state == NULL || payload == NULL) {
        return FALSE;
    }
    const int apk = mi_fitness_apk_daily_type(wire_daily_type);
    MiwearFitnessDataId id;
    MiwearFitnessBinarySlice slice;
    if (!native_split(payload, payload_len, wire_daily_type, detail, version, &slice, &id)) {
        return FALSE;
    }

    MiFitnessFieldValue values[MI_FITNESS_FIELD_VALUE_MAX];
    size_t value_count = 0;

    switch (apk) {
        case 9:
            if (mi_fitness_oneden_parse_report(mi_fitness_abnormal_record_fields,
                                               mi_fitness_abnormal_record_field_count,
                                               version,
                                               slice.data_valid,
                                               slice.data_valid_len,
                                               slice.body,
                                               slice.body_len,
                                               values,
                                               &value_count)) {
                log_field_values(state, "Abnormal health (native)", values, value_count);
                return TRUE;
            }
            break;
        case 10:
            if (detail == 0) {
                const size_t n = mi_fitness_oneden_parse_loop_records(mi_fitness_weight_record_fields,
                                                                      mi_fitness_weight_record_field_count,
                                                                      version,
                                                                      slice.data_valid,
                                                                      slice.data_valid_len,
                                                                      slice.body,
                                                                      slice.body_len);
                if (n > 0) {
                    gchar *line = g_strdup_printf("Weight sync (native): %zu record(s)", n);
                    append_log(state, line);
                    g_free(line);
                    return TRUE;
                }
            }
            break;
        case 11:
            if (mi_fitness_oneden_parse_report(mi_fitness_ecg_measure_fields,
                                               mi_fitness_ecg_measure_field_count,
                                               version,
                                               slice.data_valid,
                                               slice.data_valid_len,
                                               slice.body,
                                               slice.body_len,
                                               values,
                                               &value_count)) {
                log_field_values(state, "ECG (native)", values, value_count);
                return TRUE;
            }
            break;
        case 12:
            if (mi_fitness_oneden_parse_report(mi_fitness_temperature_measure_fields,
                                               mi_fitness_temperature_measure_field_count,
                                               version,
                                               slice.data_valid,
                                               slice.data_valid_len,
                                               slice.body,
                                               slice.body_len,
                                               values,
                                               &value_count)) {
                log_field_values(state, "Temperature (native)", values, value_count);
                return TRUE;
            }
            break;
        default:
            (void)ts;
            break;
    }
    return FALSE;
}
