#include "miwear/activity/manual.h"
#include "miwear/db/db.h"
#include "miwear/ui/dashboard.h"
#include "miwear/ui/health_data.h"

#include <json-glib/json-glib.h>
#include <string.h>

enum {
    TYPE_HR = 0x12,
    TYPE_SPO2 = 0x63,
    TYPE_STRESS = 0x13,
    TYPE_TEMPERATURE = 0x44,
};

static guint s_manual_entries_committed;

void miwear_manual_entries_reset(void) {
    s_manual_entries_committed = 0;
}

guint miwear_manual_entries_committed(void) {
    return s_manual_entries_committed;
}

enum {
    MEASURE_HR = 1,
    MEASURE_SPO2 = 2,
    MEASURE_STRESS = 3,
    MEASURE_TEMPERATURE = 4,
    MEASURE_BLOOD_PRESSURE = 5,
};

static MiwearXiaomiActivityRow manual_row_template(int32_t sample_ts) {
    MiwearXiaomiActivityRow r = {0};
    r.timestamp_sec = sample_ts;
    r.raw_intensity = -1;
    r.raw_kind = -1;
    r.steps = -1;
    r.distance_cm = -1;
    r.active_calories = -1;
    r.energy = -1;
    r.heart_rate = -1;
    r.stress = -1;
    r.spo2 = -1;
    return r;
}

static int json_obj_int_opt(JsonObject *obj, const char *key) {
    if (obj == NULL || !json_object_has_member(obj, key)) {
        return -1;
    }
    return (int)json_object_get_int_member(obj, key);
}

static int stress_from_manual_item(JsonObject *item) {
    if (item == NULL) {
        return -1;
    }
    if (json_object_has_member(item, "stressObj")) {
        JsonObject *st = json_object_get_object_member(item, "stressObj");
        const int v = json_obj_int_opt(st, "stress");
        if (miwear_stress_value_valid(v)) {
            return v;
        }
    }
    if (json_object_has_member(item, "stress")) {
        const int v = json_obj_int_opt(item, "stress");
        if (miwear_stress_value_valid(v)) {
            return v;
        }
    }
    if (json_object_has_member(item, "measurementType")) {
        JsonNode *mt = json_object_get_member(item, "measurementType");
        int mtype = -1;
        if (mt != NULL && JSON_NODE_HOLDS_OBJECT(mt)) {
            mtype = json_obj_int_opt(json_node_get_object(mt), "type");
        } else if (mt != NULL && JSON_NODE_HOLDS_VALUE(mt)) {
            mtype = (int)json_node_get_int(mt);
        }
        if (mtype == MEASURE_STRESS && json_object_has_member(item, "stressObj")) {
            JsonObject *st = json_object_get_object_member(item, "stressObj");
            const int v = json_obj_int_opt(st, "stress");
            if (miwear_stress_value_valid(v)) {
                return v;
            }
        }
    }
    return -1;
}

static void commit_manual_rows(AppState *state, uint32_t file_day_ts, GArray *act_rows, const char *log_tag) {
    const guint count = act_rows != NULL ? act_rows->len : 0;
    int latest_spo2 = -1;
    int latest_stress = -1;
    int32_t latest_spo2_ts = 0;
    int32_t latest_stress_ts = 0;

    if (count > 0) {
        for (guint i = 0; i < count; i++) {
            const MiwearXiaomiActivityRow *r = &g_array_index(act_rows, MiwearXiaomiActivityRow, i);
            if (miwear_spo2_value_valid(r->spo2) && r->timestamp_sec >= latest_spo2_ts) {
                latest_spo2_ts = r->timestamp_sec;
                latest_spo2 = r->spo2;
            }
            if (miwear_stress_value_valid(r->stress) && r->timestamp_sec >= latest_stress_ts) {
                latest_stress_ts = r->timestamp_sec;
                latest_stress = r->stress;
            }
        }
        (void)miwear_db_replace_xiaomi_activity_batch((const MiwearXiaomiActivityRow *)act_rows->data, count);
        recorded_merge_xiaomi_activity_rows(state, (const MiwearXiaomiActivityRow *)act_rows->data, count);
    }

    gchar *spo2_txt = NULL;
    gchar *stress_txt = NULL;
    if (latest_spo2 >= 0) {
        spo2_txt = g_strdup_printf("%d%%", latest_spo2);
    }
    if (latest_stress >= 0) {
        stress_txt = g_strdup_printf("%d", latest_stress);
        state->live_stress_ts_sec = latest_stress_ts;
    }
    if (spo2_txt != NULL || stress_txt != NULL) {
        schedule_metrics_update(state, FALSE, 0, FALSE, 0, FALSE, 0, FALSE, 0, FALSE, 0, FALSE, 0, spo2_txt, stress_txt);
    }
    g_free(spo2_txt);
    g_free(stress_txt);
    miwear_dashboard_refresh(state);

    gchar *summary = NULL;
    if (latest_stress >= 0) {
        summary = g_strdup_printf("%s: %u health entries applied (file day ts=%u, latest stress=%d)",
                                  log_tag,
                                  count,
                                  file_day_ts,
                                  latest_stress);
    } else {
        summary = g_strdup_printf("%s: %u health entries applied (file day ts=%u)", log_tag, count, file_day_ts);
    }
    append_log(state, summary);
    g_free(summary);
    if (count > 0) {
        s_manual_entries_committed = count;
    }
}

static void manual_log_body_prefix(AppState *state, const uint8_t *payload, size_t body_off, size_t body_len) {
    if (state == NULL || payload == NULL || body_off >= body_len) {
        return;
    }
    const size_t n = body_len - body_off < 16 ? body_len - body_off : 16;
    GString *hex = g_string_new("");
    for (size_t i = 0; i < n; i++) {
        g_string_append_printf(hex, "%02X ", payload[body_off + i]);
    }
    gchar *line = g_strdup_printf("Manual samples debug: body+%zu first bytes: %s", body_off, hex->str);
    append_log(state, line);
    g_free(line);
    g_string_free(hex, TRUE);
}

static void json_harvest_manual_row(JsonObject *item, int32_t fallback_ts, GArray *act_rows, AppState *state) {
    if (item == NULL || act_rows == NULL) {
        return;
    }
    int32_t sample_ts = fallback_ts;
    if (json_object_has_member(item, "timestamp")) {
        sample_ts = (int32_t)json_object_get_int_member(item, "timestamp");
    }
    if (sample_ts <= 0) {
        sample_ts = fallback_ts;
    }

    MiwearXiaomiActivityRow r = manual_row_template(sample_ts);
    gboolean have = FALSE;

    if (json_object_has_member(item, "heartRateObj")) {
        JsonObject *hr = json_object_get_object_member(item, "heartRateObj");
        const int v = json_obj_int_opt(hr, "heartRate");
        if (v > 0 && v < 255) {
            r.heart_rate = v;
            have = TRUE;
        }
    }
    if (json_object_has_member(item, "spo2Obj")) {
        JsonObject *sp = json_object_get_object_member(item, "spo2Obj");
        const int v = json_obj_int_opt(sp, "spo2");
        if (miwear_spo2_value_valid(v)) {
            r.spo2 = v;
            have = TRUE;
        }
    }
    const int stress_v = stress_from_manual_item(item);
    if (miwear_stress_value_valid(stress_v)) {
        r.stress = stress_v;
        have = TRUE;
        if (state != NULL) {
            gchar *line = g_strdup_printf("Manual stress sample (schema): ts=%d value=%d", sample_ts, stress_v);
            append_log(state, line);
            g_free(line);
        }
    }
    if (have) {
        g_array_append_val(act_rows, r);
    }
}

static void json_harvest_manual_recursive(JsonNode *node, int32_t fallback_ts, GArray *act_rows, AppState *state) {
    if (node == NULL || act_rows == NULL) {
        return;
    }
    if (JSON_NODE_HOLDS_OBJECT(node)) {
        JsonObject *obj = json_node_get_object(node);
        json_harvest_manual_row(obj, fallback_ts, act_rows, state);
        GList *members = json_object_get_members(obj);
        for (GList *l = members; l != NULL; l = l->next) {
            const char *name = l->data;
            JsonNode *child = json_object_get_member(obj, name);
            if (child != NULL && (JSON_NODE_HOLDS_ARRAY(child) || JSON_NODE_HOLDS_OBJECT(child))) {
                json_harvest_manual_recursive(child, fallback_ts, act_rows, state);
            }
        }
        g_list_free(members);
        return;
    }
    if (JSON_NODE_HOLDS_ARRAY(node)) {
        JsonArray *arr = json_node_get_array(node);
        const guint n = json_array_get_length(arr);
        for (guint i = 0; i < n; i++) {
            json_harvest_manual_recursive(json_array_get_element(arr, i), fallback_ts, act_rows, state);
        }
    }
}

guint miwear_apply_manual_measurement_json(AppState *state, uint32_t file_day_ts, JsonNode *decoded) {
    if (state == NULL || decoded == NULL || !JSON_NODE_HOLDS_OBJECT(decoded)) {
        return 0;
    }
    JsonObject *root = json_node_get_object(decoded);
    GArray *act_rows = g_array_new(FALSE, FALSE, sizeof(MiwearXiaomiActivityRow));

    if (json_object_has_member(root, "body")) {
        JsonNode *body_node = json_object_get_member(root, "body");
        if (body_node != NULL && JSON_NODE_HOLDS_ARRAY(body_node)) {
            JsonArray *body = json_node_get_array(body_node);
            for (guint i = 0; i < json_array_get_length(body); i++) {
                JsonNode *el = json_array_get_element(body, i);
                if (el != NULL && JSON_NODE_HOLDS_OBJECT(el)) {
                    json_harvest_manual_row(json_node_get_object(el), (int32_t)file_day_ts, act_rows, state);
                }
            }
        } else if (body_node != NULL) {
            json_harvest_manual_recursive(body_node, (int32_t)file_day_ts, act_rows, state);
        }
    }
    json_harvest_manual_recursive(decoded, (int32_t)file_day_ts, act_rows, state);

    const guint n = act_rows->len;
    if (n == 0 && json_object_has_member(root, "body")) {
        JsonNode *body_node = json_object_get_member(root, "body");
        if (body_node != NULL && JSON_NODE_HOLDS_ARRAY(body_node)) {
            gchar *line = g_strdup_printf(
                "Manual samples (schema): decoded body array len=%u but no HR/SpO₂/stress values matched.",
                json_array_get_length(json_node_get_array(body_node)));
            append_log(state, line);
            g_free(line);
        }
    }
    if (n > 0) {
        commit_manual_rows(state, file_day_ts, act_rows, "Manual samples (schema)");
    }
    g_array_unref(act_rows);
    return n;
}

static size_t manual_measure_payload_len(int measure_type, guint data_len) {
    if (data_len > 0) {
        return (size_t)data_len;
    }
    switch (measure_type) {
        case MEASURE_HR:
        case MEASURE_SPO2:
        case MEASURE_STRESS:
            return 1;
        case MEASURE_TEMPERATURE:
            return 4;
        case MEASURE_BLOOD_PRESSURE:
            return 6;
        default:
            return 1;
    }
}

static size_t manual_body_start(const uint8_t *payload, size_t body_len, int version) {
    if (version >= 2 && body_len > 8) {
        return 8;
    }
    size_t off = 7;
    if (off < body_len && payload[off] != 0) {
        off += 1;
    }
    return off;
}

static guint scan_legacy_manual_records(AppState *state,
                                        const uint8_t *payload,
                                        size_t body_len,
                                        size_t body_off,
                                        GArray *act_rows) {
    guint found = 0;
    for (size_t off = body_off; off + 5 <= body_len; off++) {
        const uint32_t sample_ts = miwear_read_u32_le(payload + off);
        if (sample_ts < 1000000000u || sample_ts > 2000000000u) {
            continue;
        }
        const uint8_t typ = payload[off + 4];
        int value = -1;
        if (typ == TYPE_STRESS || typ == (uint8_t)(MEASURE_STRESS | (1u << 4))) {
            value = payload[off + 5];
        } else if (typ == 0x63 || (typ & 0x0F) == MEASURE_STRESS) {
            value = payload[off + 5];
        } else if (typ == TYPE_HR || (typ & 0x0F) == MEASURE_HR) {
            value = payload[off + 5];
            if (value > 0 && value < 255) {
                MiwearXiaomiActivityRow r = manual_row_template((int32_t)sample_ts);
                r.heart_rate = value;
                g_array_append_val(act_rows, r);
                found++;
            }
            continue;
        } else {
            continue;
        }
        if (!miwear_stress_value_valid(value)) {
            continue;
        }
        MiwearXiaomiActivityRow r = manual_row_template((int32_t)sample_ts);
        r.stress = value;
        g_array_append_val(act_rows, r);
        found++;
        if (state != NULL) {
            gchar *line = g_strdup_printf(
                "Manual stress sample (legacy scan): ts=%u value=%d at off=%zu typ=0x%02X",
                sample_ts,
                value,
                off,
                typ);
            append_log(state, line);
            g_free(line);
        }
    }
    return found;
}

static gboolean parse_v2_measure_sample(AppState *state,
                                        const uint8_t *payload,
                                        size_t body_len,
                                        size_t *inout_off,
                                        MiwearXiaomiActivityRow *out_row) {
    size_t off = *inout_off;
    if (off + 5 > body_len) {
        return FALSE;
    }
    const uint32_t sample_ts = miwear_read_u32_le(payload + off);
    const uint8_t typ_byte = payload[off + 4];
    const int measure_type = typ_byte & 0x0F;
    const guint data_len = typ_byte >> 4;
    const size_t value_off = off + 5;
    const size_t payload_len = manual_measure_payload_len(measure_type, data_len);
    if (value_off + payload_len > body_len) {
        return FALSE;
    }

    MiwearXiaomiActivityRow r = manual_row_template((int32_t)sample_ts);
    gboolean parsed = FALSE;

    switch (measure_type) {
        case MEASURE_HR: {
            const int v = payload[value_off];
            if (v > 0 && v < 255) {
                r.heart_rate = v;
                parsed = TRUE;
            }
            break;
        }
        case MEASURE_SPO2: {
            const int v = payload[value_off];
            if (miwear_spo2_value_valid(v)) {
                r.spo2 = v;
                parsed = TRUE;
            }
            break;
        }
        case MEASURE_STRESS: {
            const int v = payload[value_off];
            if (miwear_stress_value_valid(v)) {
                r.stress = v;
                parsed = TRUE;
            }
            break;
        }
        case MEASURE_TEMPERATURE:
            if (payload_len >= 4) {
                const uint32_t value = miwear_read_u32_le(payload + value_off);
                if (state != NULL) {
                    gchar *tline =
                        g_strdup_printf("Manual temperature: %.2f (raw u32=%u)", value / 100.0, value);
                    append_log(state, tline);
                    g_free(tline);
                }
            }
            break;
        case MEASURE_BLOOD_PRESSURE:
            break;
        default:
            break;
    }

    *inout_off = value_off + payload_len;
    if (parsed && measure_type == MEASURE_STRESS && state != NULL) {
        gchar *line = g_strdup_printf(
            "Manual stress sample (wire v2): ts=%u value=%d dataLen=%u",
            sample_ts,
            r.stress,
            data_len);
        append_log(state, line);
        g_free(line);
    }
    if (parsed) {
        *out_row = r;
    }
    return parsed;
}

static void apply_manual_samples_v2(AppState *state, uint32_t file_day_ts, const uint8_t *payload, size_t payload_len) {
    const int version = payload_len > 5 ? payload[5] : 0;
    const size_t body_len = payload_len >= 4 ? payload_len - 4 : 0;
    const size_t body_off = manual_body_start(payload, body_len, version);
    size_t off = body_off;
    GArray *act_rows = g_array_new(FALSE, FALSE, sizeof(MiwearXiaomiActivityRow));

    while (off + 5 <= body_len) {
        const size_t before = off;
        const uint8_t typ_byte = payload[off + 4];
        const int v2_type = typ_byte & 0x0F;
        const guint data_len = typ_byte >> 4;
        if (v2_type >= MEASURE_HR && v2_type <= MEASURE_BLOOD_PRESSURE) {
            MiwearXiaomiActivityRow r;
            if (parse_v2_measure_sample(state, payload, body_len, &off, &r)) {
                g_array_append_val(act_rows, r);
            } else {
                off = before + 5 + (data_len > 0 ? data_len : 1u);
            }
            continue;
        }
        off = before + 5 + (data_len > 0 ? data_len : 1u);
    }

    if (act_rows->len == 0) {
        (void)scan_legacy_manual_records(state, payload, body_len, body_off, act_rows);
    }

    if (act_rows->len == 0) {
        manual_log_body_prefix(state, payload, body_off, body_len);
    }

    commit_manual_rows(state, file_day_ts, act_rows, "Manual samples (v2 wire)");
    g_array_unref(act_rows);
}

void miwear_apply_manual_samples(AppState *state, uint32_t file_day_ts, const uint8_t *payload, size_t payload_len) {
    if (payload_len > 5 && payload[5] >= 2) {
        apply_manual_samples_v2(state, file_day_ts, payload, payload_len);
        return;
    }

    if (payload_len < 13) {
        append_log(state, "Manual samples: payload too short.");
        return;
    }

    const size_t body_len = payload_len - 4;
    size_t off = 7;
    if (off >= body_len) {
        append_log(state, "Manual samples: missing body.");
        return;
    }
    if (payload[off] != 0) {
        off += 1;
    }

    GArray *act_rows = g_array_new(FALSE, FALSE, sizeof(MiwearXiaomiActivityRow));

    while (off + 5 <= body_len) {
        const uint32_t sample_ts = miwear_read_u32_le(payload + off);
        const uint8_t typ = payload[off + 4];
        const int v2_type = typ & 0x0F;

        if (v2_type >= MEASURE_HR && v2_type <= MEASURE_BLOOD_PRESSURE) {
            MiwearXiaomiActivityRow r;
            if (parse_v2_measure_sample(state, payload, body_len, &off, &r)) {
                g_array_append_val(act_rows, r);
            }
            continue;
        }

        off += 5;
        uint32_t value = 0;
        if (typ == TYPE_HR || typ == TYPE_SPO2 || typ == TYPE_STRESS) {
            if (off >= body_len) {
                break;
            }
            value = payload[off++];
        } else if (typ == TYPE_TEMPERATURE) {
            if (off + 4 > body_len) {
                break;
            }
            value = miwear_read_u32_le(payload + off);
            off += 4;
        } else {
            gchar *line = g_strdup_printf("Manual samples: unknown type 0x%02X", typ);
            append_log(state, line);
            g_free(line);
            break;
        }

        if (value == 0) {
            continue;
        }

        MiwearXiaomiActivityRow r = manual_row_template((int32_t)sample_ts);
        if (typ == TYPE_HR) {
            r.heart_rate = (int)value;
        } else if (typ == TYPE_SPO2) {
            r.spo2 = (int)value;
        } else if (typ == TYPE_STRESS) {
            r.stress = (int)value;
        }

        if (typ == TYPE_TEMPERATURE) {
            gchar *tline = g_strdup_printf("Manual temperature: %.2f (raw u32=%u)", value / 100.0, value);
            append_log(state, tline);
            g_free(tline);
            continue;
        }

        if (typ == TYPE_HR || typ == TYPE_SPO2 || typ == TYPE_STRESS) {
            g_array_append_val(act_rows, r);
        }
    }

    commit_manual_rows(state, file_day_ts, act_rows, "Manual samples");
    g_array_unref(act_rows);
}
