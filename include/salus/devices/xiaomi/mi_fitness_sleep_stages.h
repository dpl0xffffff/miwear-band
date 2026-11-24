#ifndef SALUS_ACTIVITY_MI_FITNESS_SLEEP_STAGES_H
#define SALUS_ACTIVITY_MI_FITNESS_SLEEP_STAGES_H

#include "salus/devices/xiaomi/mi_fitness_full_day_sleep.h"
#include "salus/app.h"

#include <json-glib/json-glib.h>
#include <stddef.h>
#include <stdint.h>

/* Mi Fitness: AllDaySleepSchemaParser → featureData → splitAllDaySleepBytes → MiDevSleepAlgo.parseSleep. */

void mi_fitness_apply_sleep_stages_from_feature(AppState *state,
                                                const uint8_t *full_payload,
                                                size_t full_payload_len,
                                                int version,
                                                const uint8_t *feature_data,
                                                size_t feature_len,
                                                uint32_t bed_sec,
                                                uint32_t wake_sec);


#endif
