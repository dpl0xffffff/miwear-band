#ifndef MIWEAR_SLEEP_MI_SLEEP_DURATION_H
#define MIWEAR_SLEEP_MI_SLEEP_DURATION_H

#include <stdint.h>

/* Mi Fitness SleepStageSummary stores stage lengths in minutes; some helpers emit seconds. */
#define MIWEAR_SLEEP_MAX_PLAUSIBLE_MINUTES 1440
#define MIWEAR_SLEEP_MAX_PLAUSIBLE_STAGE_MINUTES 900

typedef struct MiwearSleepStageDurations {
    int deep_min;
    int light_min;
    int rem_min;
    int awake_min;
    int total_min;
} MiwearSleepStageDurations;

void miwear_sleep_normalize_stage_durations(MiwearSleepStageDurations *d);

int miwear_sleep_clamp_stage_minutes(int minutes);

#endif
