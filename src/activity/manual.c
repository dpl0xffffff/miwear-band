#include "miwear/activity/manual.h"
#include "miwear/db/db.h"
#include "miwear/ui/dashboard.h"
#include "miwear/ui/health_data.h"

#include <string.h>

enum {
    TYPE_HR = 0x12,
    TYPE_SPO2 = 0x63,
    TYPE_STRESS = 0x13,
    TYPE_TEMPERATURE = 0x44,
};

void miwear_apply_manual_samples(AppState *state, uint32_t file_day_ts, const uint8_t *payload, size_t payload_len) {
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
    guint count = 0;

    while (off + 5 <= body_len) {
        const uint32_t sample_ts = miwear_read_u32_le(payload + off);
        const uint8_t typ = payload[off + 4];
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

        MiwearXiaomiActivityRow r = {0};
        r.timestamp_sec = (int32_t)sample_ts;
        r.raw_intensity = -1;
        r.raw_kind = -1;
        r.steps = -1;
        r.distance_cm = -1;
        r.active_calories = -1;
        r.energy = -1;
        r.heart_rate = -1;
        r.stress = -1;
        r.spo2 = -1;

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
            count++;
        }
    }

    int latest_spo2 = -1;
    int latest_stress = -1;
    int32_t latest_spo2_ts = 0;
    int32_t latest_stress_ts = 0;

    if (act_rows->len > 0) {
        for (guint i = 0; i < act_rows->len; i++) {
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
        (void)miwear_db_replace_xiaomi_activity_batch((const MiwearXiaomiActivityRow *)act_rows->data, act_rows->len);
        recorded_merge_xiaomi_activity_rows(state, (const MiwearXiaomiActivityRow *)act_rows->data, act_rows->len);
    }
    g_array_unref(act_rows);

    gchar *spo2_txt = NULL;
    gchar *stress_txt = NULL;
    if (latest_spo2 >= 0) {
        spo2_txt = g_strdup_printf("%d%%", latest_spo2);
    }
    if (latest_stress >= 0) {
        stress_txt = g_strdup_printf("%d", latest_stress);
    }
    if (spo2_txt != NULL || stress_txt != NULL) {
        schedule_metrics_update(state, FALSE, 0, FALSE, 0, FALSE, 0, FALSE, 0, FALSE, 0, FALSE, 0, spo2_txt, stress_txt);
    }
    g_free(spo2_txt);
    g_free(stress_txt);
    miwear_dashboard_refresh(state);

    gchar *summary = g_strdup_printf("Manual samples: %u health entries applied (file day ts=%u)", count, file_day_ts);
    append_log(state, summary);
    g_free(summary);
}
