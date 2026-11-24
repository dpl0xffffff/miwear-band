#ifndef SALUS_SLEEP_MI_SLEEP_DURATION_H
#define SALUS_SLEEP_MI_SLEEP_DURATION_H

#include <stdint.h>

/* Mi Fitness SleepStageSummary stores stage lengths in minutes; some helpers emit seconds. */
#define SALUS_SLEEP_MAX_PLAUSIBLE_MINUTES 1440
#define SALUS_SLEEP_MAX_PLAUSIBLE_STAGE_MINUTES 900

typedef struct SalusSleepStageDurations {
    int deep_min;
    int light_min;
    int rem_min;
    int awake_min;
    int total_min;
} SalusSleepStageDurations;

void salus_sleep_normalize_stage_durations(SalusSleepStageDurations *d);

int salus_sleep_clamp_stage_minutes(int minutes);

#endif
