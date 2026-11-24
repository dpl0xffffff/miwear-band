#ifndef SALUS_SLEEP_MI_SLEEP_EXTERNAL_H
#define SALUS_SLEEP_MI_SLEEP_EXTERNAL_H

#include "salus/app.h"

#include <stdint.h>

gboolean salus_sleep_run_external_algo_cmd(AppState *state,
                                            const uint8_t *sleep_src,
                                            size_t sleep_src_len,
                                            GArray *stage_rows,
                                            int *deep_min,
                                            int *light_min,
                                            int *rem_min,
                                            int *awake_min,
                                            int *total_min,
                                            uint32_t bed_sec,
                                            uint32_t wake_sec);

#endif
