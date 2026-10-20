#ifndef MIWEAR_SLEEP_MI_SLEEP_EXTERNAL_H
#define MIWEAR_SLEEP_MI_SLEEP_EXTERNAL_H

#include "miwear/app.h"

#include <stdint.h>

gboolean miwear_sleep_run_external_algo_cmd(AppState *state,
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
