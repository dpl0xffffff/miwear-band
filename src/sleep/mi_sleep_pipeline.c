#include "miwear/activity/activity.h"
#include "miwear/sleep/mi_sleep_packets.h"
#include "miwear/sleep/mi_sleep_stage.h"

#include <glib.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct MiwearSleepTfliteModel MiwearSleepTfliteModel;

extern MiwearSleepTfliteModel *miwear_sleep_tflite_load(const char *model_path);
extern void miwear_sleep_tflite_unload(MiwearSleepTfliteModel *ctx);
extern int miwear_sleep_tflite_classify_epochs(MiwearSleepTfliteModel *ctx,
                                               const float *features,
                                               size_t epoch_count,
                                               float *pred_out,
                                               size_t pred_stride);

size_t mi_sleep_build_hr_epoch_features(const uint8_t *sleep_src,
                                        size_t sleep_src_len,
                                        uint32_t bed_sec,
                                        uint32_t wake_sec,
                                        float *features_out,
                                        size_t max_epochs);

void miwear_sleep_algo_result_clear(MiwearSleepAlgoResult *result) {
    if (result == NULL) {
        return;
    }
    g_free(result->stage_rows);
    g_free(result->log_note);
    memset(result, 0, sizeof(*result));
}

static int argmax4(const float *p) {
    int best = 0;
    for (int i = 1; i < 4; i++) {
        if (p[i] > p[best]) {
            best = i;
        }
    }
    return best;
}

static void durations_from_states(const int *states,
                                  size_t n,
                                  uint32_t bed_sec,
                                  uint32_t wake_sec,
                                  MiwearSleepStageDurations *d) {
    if (d == NULL || states == NULL || n == 0) {
        return;
    }
    int deep_s = 0, light_s = 0, rem_s = 0, wake_s = 0;
    for (size_t i = 0; i < n; i++) {
        switch (states[i]) {
            case 2:
                deep_s += MI_SLEEP_EPOCH_SEC;
                break;
            case 1:
                light_s += MI_SLEEP_EPOCH_SEC;
                break;
            case 3:
                rem_s += MI_SLEEP_EPOCH_SEC;
                break;
            case 0:
                wake_s += MI_SLEEP_EPOCH_SEC;
                break;
            default:
                break;
        }
    }
    d->deep_min = deep_s / 60;
    d->light_min = light_s / 60;
    d->rem_min = rem_s / 60;
    d->awake_min = wake_s / 60;
    if (wake_sec > bed_sec) {
        d->total_min = (int)((wake_sec - bed_sec) / 60u);
    } else {
        d->total_min = (int)((n * MI_SLEEP_EPOCH_SEC) / 60);
    }
    miwear_sleep_normalize_stage_durations(d);
}

static MiwearSleepStageRow *build_stage_segments(const int *states,
                                                 size_t n,
                                                 int64_t bed_ms,
                                                 int64_t wake_ms,
                                                 size_t *count_out) {
    if (count_out == NULL || states == NULL || n == 0) {
        return NULL;
    }
    *count_out = 0;
    int64_t step_ms = MI_SLEEP_EPOCH_SEC * 1000LL;
    if (wake_ms > bed_ms && n > 0) {
        step_ms = (wake_ms - bed_ms) / (int64_t)n;
        if (step_ms <= 0) {
            step_ms = MI_SLEEP_EPOCH_SEC * 1000LL;
        }
    }
    size_t cap = 32;
    size_t count = 0;
    MiwearSleepStageRow *rows = g_new(MiwearSleepStageRow, cap);
    int cur = states[0];
    int64_t seg_start = bed_ms;
    for (size_t i = 1; i < n; i++) {
        if (states[i] != cur) {
            if (count >= cap) {
                cap *= 2;
                rows = g_realloc(rows, cap * sizeof(MiwearSleepStageRow));
            }
            rows[count].timestamp_ms = seg_start;
            rows[count].stage = miwear_normalize_sleep_stage(cur);
            count++;
            cur = states[i];
            seg_start = bed_ms + (int64_t)i * step_ms;
        }
    }
    if (count >= cap) {
        cap++;
        rows = g_realloc(rows, cap * sizeof(MiwearSleepStageRow));
    }
    rows[count].timestamp_ms = seg_start;
    rows[count].stage = miwear_normalize_sleep_stage(cur);
    count++;
    *count_out = count;
    return rows;
}

static const char *default_model_path(void) {
    const char *env = g_getenv("MIWEAR_SLEEP_MODEL");
    if (env != NULL && env[0] != '\0') {
        return env;
    }
    return MIWEAR_SLEEP_MODEL_DEFAULT;
}

gboolean miwear_sleep_run_mi_fitness_algo(const uint8_t *sleep_src,
                                          size_t sleep_src_len,
                                          uint32_t bed_sec,
                                          uint32_t wake_sec,
                                          MiwearSleepAlgoResult *result_out) {
    if (sleep_src == NULL || sleep_src_len == 0 || result_out == NULL) {
        return FALSE;
    }
    memset(result_out, 0, sizeof(*result_out));

    MiwearSleepStageDurations embedded_d = {0};
    MiwearSleepStageRow *embedded_rows = NULL;
    size_t embedded_n = 0;
    if (mi_sleep_try_embedded_stage_packets(sleep_src,
                                            sleep_src_len,
                                            bed_sec,
                                            wake_sec,
                                            &embedded_rows,
                                            &embedded_n,
                                            &embedded_d)) {
        result_out->stage_rows = embedded_rows;
        result_out->stage_count = embedded_n;
        result_out->durations = embedded_d;
        if (wake_sec > bed_sec && result_out->durations.total_min <= 0) {
            result_out->durations.total_min = (int)((wake_sec - bed_sec) / 60u);
        }
        miwear_sleep_normalize_stage_durations(&result_out->durations);
        result_out->log_note = g_strdup("Embedded sleep stage packets (type 17/16).");
        return TRUE;
    }

    float *features = g_new0(float, MI_SLEEP_MAX_EPOCHS * MI_SLEEP_FEATURE_DIM);
    size_t epochs =
        mi_sleep_build_hr_epoch_features(sleep_src, sleep_src_len, bed_sec, wake_sec, features, MI_SLEEP_MAX_EPOCHS);
    if (epochs == 0) {
        g_free(features);
        return FALSE;
    }

    MiwearSleepTfliteModel *model = miwear_sleep_tflite_load(default_model_path());
    if (model == NULL) {
        g_free(features);
        result_out->log_note = g_strdup("TFLite model unavailable (build with MIWEAR_ENABLE_SLEEP_ML=ON).");
        return FALSE;
    }

    int *states = g_new(int, epochs);
    float *preds = g_new(float, epochs * 4);
    const int rc = miwear_sleep_tflite_classify_epochs(model, features, epochs, preds, 4);
    miwear_sleep_tflite_unload(model);
    g_free(features);
    if (rc <= 0) {
        g_free(states);
        g_free(preds);
        return FALSE;
    }
    for (size_t i = 0; i < epochs; i++) {
        states[i] = argmax4(preds + i * 4);
    }
    g_free(preds);

    const int64_t bed_ms = (int64_t)bed_sec * 1000LL;
    const int64_t wake_ms = (int64_t)wake_sec * 1000LL;
    result_out->stage_rows = build_stage_segments(states, epochs, bed_ms, wake_ms, &result_out->stage_count);
    durations_from_states(states, epochs, bed_sec, wake_sec, &result_out->durations);
    g_free(states);
    result_out->log_note = g_strdup("Linux native sleep pipeline (HR epoch features + TFLite).");
    return result_out->stage_count > 0;
}
