#ifndef MIWEAR_SLEEP_MI_SLEEP_STAGE_H
#define MIWEAR_SLEEP_MI_SLEEP_STAGE_H

#include "miwear/db/db.h"
#include "miwear/sleep/mi_sleep_duration.h"

#include <stddef.h>
#include <stdint.h>

typedef struct MiwearSleepAlgoResult {
    MiwearSleepStageDurations durations;
    MiwearSleepStageRow *stage_rows;
    size_t stage_count;
    char *log_note;
} MiwearSleepAlgoResult;

void miwear_sleep_algo_result_clear(MiwearSleepAlgoResult *result);

/* Sleep stages from the band's embedded stage packets (types 16/17). FALSE when the
 * feature blob carries none; the caller then tries MIWEAR_MI_SLEEP_ALGO_CMD. */
gboolean miwear_sleep_run_mi_fitness_algo(const uint8_t *sleep_src,
                                          size_t sleep_src_len,
                                          uint32_t bed_sec,
                                          uint32_t wake_sec,
                                          MiwearSleepAlgoResult *result_out);

#endif
