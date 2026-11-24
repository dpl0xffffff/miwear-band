#ifndef SALUS_SLEEP_MI_SLEEP_STAGE_H
#define SALUS_SLEEP_MI_SLEEP_STAGE_H

#include "salus/store/db.h"
#include "salus/store/sleep.h"

#include <stddef.h>
#include <stdint.h>

typedef struct SalusSleepAlgoResult {
    SalusSleepStageDurations durations;
    SalusSleepStageRow *stage_rows;
    size_t stage_count;
    char *log_note;
} SalusSleepAlgoResult;

void salus_sleep_algo_result_clear(SalusSleepAlgoResult *result);

/* Sleep stages from the band's embedded stage packets (types 16/17). FALSE when the
 * feature blob carries none; the caller then tries SALUS_MI_SLEEP_ALGO_CMD. */
gboolean salus_sleep_run_mi_fitness_algo(const uint8_t *sleep_src,
                                          size_t sleep_src_len,
                                          uint32_t bed_sec,
                                          uint32_t wake_sec,
                                          SalusSleepAlgoResult *result_out);

#endif
