#include "miwear/activity/mi_fitness_sleep_stages.h"
#include "miwear/activity/mi_fitness_full_day_sleep.h"
#include "miwear/activity/activity.h"
#include "miwear/db/db.h"
#include "miwear/sleep/mi_sleep_external.h"
#include "miwear/sleep/mi_sleep_stage.h"
#include "miwear/app.h"
#include "miwear/ui/dashboard.h"

#include <json-glib/json-glib.h>
#include <string.h>

static const uint8_t *mi_fitness_sleep_src_bytes(const uint8_t *full_payload,
                                                 size_t full_payload_len,
                                                 const uint8_t *feature_data,
                                                 size_t feature_len,
                                                 size_t *sleep_src_len_out) {
    if (sleep_src_len_out == NULL) {
        return NULL;
    }
    *sleep_src_len_out = 0;
    if (feature_data == NULL || feature_len == 0) {
        return NULL;
    }
    if (full_payload != NULL && full_payload_len >= feature_len) {
        const uint8_t *tail = full_payload + (full_payload_len - feature_len);
        if (feature_data >= full_payload && feature_data + feature_len <= full_payload + full_payload_len &&
            tail == feature_data) {
            *sleep_src_len_out = feature_len;
            return feature_data;
        }
    }
    *sleep_src_len_out = feature_len;
    return feature_data;
}

static void persist_stage_timeline(AppState *state,
                                   const MiwearSleepStageRow *rows,
                                   size_t n,
                                   int deep_min,
                                   int light_min,
                                   int rem_min,
                                   int awake_min,
                                   int total_min,
                                   int64_t bed_ms,
                                   int64_t wake_ms) {
    if (n > 0) {
        (void)miwear_db_replace_sleep_stage_samples(rows, n);
    }
    if (bed_ms > 0 && wake_ms > 0) {
        (void)miwear_db_replace_sleep_details_header(bed_ms,
                                                     wake_ms,
                                                     1,
                                                     total_min > 0 ? total_min : -1,
                                                     deep_min >= 0 ? deep_min : -1,
                                                     light_min >= 0 ? light_min : -1,
                                                     rem_min >= 0 ? rem_min : -1,
                                                     awake_min >= 0 ? awake_min : -1);
    }
    if (n > 0) {
        gchar *detail = g_strdup_printf(
            "Mi Fitness sleep algo: stage timeline stored (%zu markers, bed→wake %lld…%lld ms).",
            n,
            (long long)bed_ms,
            (long long)wake_ms);
        append_log(state, detail);
        g_free(detail);
        recorded_rebuild_from_db(state);
        miwear_dashboard_schedule_refresh(state);
    } else if (bed_ms > 0 && wake_ms > bed_ms) {
        recorded_rebuild_from_db(state);
        miwear_dashboard_schedule_refresh(state);
    }
}

void mi_fitness_apply_sleep_stages_from_feature(AppState *state,
                                                const uint8_t *full_payload,
                                                size_t full_payload_len,
                                                int version,
                                                const uint8_t *feature_data,
                                                size_t feature_len,
                                                uint32_t bed_sec,
                                                uint32_t wake_sec) {
    if (state == NULL || feature_data == NULL || feature_len == 0) {
        return;
    }

    size_t sleep_src_len = 0;
    const uint8_t *sleep_src =
        mi_fitness_sleep_src_bytes(full_payload, full_payload_len, feature_data, feature_len, &sleep_src_len);
    if (sleep_src == NULL || sleep_src_len == 0) {
        return;
    }

    GArray *stage_rows = g_array_new(FALSE, FALSE, sizeof(MiwearSleepStageRow));
    int deep_min = -1, light_min = -1, rem_min = -1, awake_min = -1, total_min = -1;

    gchar *line = g_strdup_printf(
        "Mi Fitness sleep pipeline: feature=%zu bytes, sleepSrc=%zu",
        feature_len,
        sleep_src_len);
    append_log(state, line);
    g_free(line);

    MiwearSleepAlgoResult algo = {0};
    gboolean ok = miwear_sleep_run_mi_fitness_algo(sleep_src, sleep_src_len, bed_sec, wake_sec, &algo);
    if (!ok) {
        ok = miwear_sleep_run_external_algo_cmd(state,
                                                sleep_src,
                                                sleep_src_len,
                                                stage_rows,
                                                &deep_min,
                                                &light_min,
                                                &rem_min,
                                                &awake_min,
                                                &total_min,
                                                bed_sec,
                                                wake_sec);
    } else {
        deep_min = algo.durations.deep_min;
        light_min = algo.durations.light_min;
        rem_min = algo.durations.rem_min;
        awake_min = algo.durations.awake_min;
        total_min = algo.durations.total_min;
        const int asleep_stages = (deep_min > 0 ? deep_min : 0) + (light_min > 0 ? light_min : 0) +
                                  (rem_min > 0 ? rem_min : 0);
        if (total_min <= 0 && asleep_stages > 0) {
            total_min = asleep_stages;
        } else if (wake_sec > bed_sec && asleep_stages > 0 && awake_min > 0) {
            const int bed_span = (int)((wake_sec - bed_sec) / 60u);
            if (total_min == bed_span || total_min <= 0) {
                total_min = asleep_stages;
            }
        }
        for (size_t i = 0; i < algo.stage_count; i++) {
            g_array_append_val(stage_rows, algo.stage_rows[i]);
        }
        if (algo.log_note != NULL) {
            append_log(state, algo.log_note);
        }
        miwear_sleep_algo_result_clear(&algo);
    }
    if (!ok) {
        append_log(state,
                   "Mi Fitness sleep stages: no embedded stage packets (set MIWEAR_MI_SLEEP_ALGO_CMD "
                   "for an external helper).");
        g_array_unref(stage_rows);
        (void)version;
        return;
    }

    persist_stage_timeline(state,
                           (const MiwearSleepStageRow *)stage_rows->data,
                           stage_rows->len,
                           deep_min,
                           light_min,
                           rem_min,
                           awake_min,
                           total_min,
                           (int64_t)bed_sec * 1000LL,
                           (int64_t)wake_sec * 1000LL);
    g_array_unref(stage_rows);
}

void mi_fitness_apply_sleep_stages_from_schema_body(AppState *state,
                                                    JsonObject *body,
                                                    const uint8_t *full_payload,
                                                    size_t full_payload_len,
                                                    uint32_t bed_sec,
                                                    uint32_t wake_sec) {
    if (state == NULL || body == NULL || !json_object_has_member(body, "featureData")) {
        return;
    }
    const char *b64 = json_object_get_string_member(body, "featureData");
    if (b64 == NULL || b64[0] == '\0') {
        return;
    }
    gsize raw_len = 0;
    guchar *raw = g_base64_decode(b64, &raw_len);
    if (raw == NULL || raw_len == 0) {
        g_free(raw);
        return;
    }
    mi_fitness_apply_sleep_stages_from_feature(state,
                                               full_payload,
                                               full_payload_len,
                                               0,
                                               raw,
                                               raw_len,
                                               bed_sec,
                                               wake_sec);
    g_free(raw);
}
