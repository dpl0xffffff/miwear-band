#include "miwear/activity/activity.h"
#include "miwear/sleep/mi_sleep_packets.h"
#include "miwear/sleep/mi_sleep_stage.h"

#include <glib.h>
#include <string.h>

void miwear_sleep_algo_result_clear(MiwearSleepAlgoResult *result) {
    if (result == NULL) {
        return;
    }
    g_free(result->stage_rows);
    g_free(result->log_note);
    memset(result, 0, sizeof(*result));
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

    return FALSE;
}
