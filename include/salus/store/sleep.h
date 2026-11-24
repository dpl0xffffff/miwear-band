#ifndef SALUS_STORE_SLEEP_H
#define SALUS_STORE_SLEEP_H

#include <stdint.h>

/* Stored sleep-stage values (Gadgetbridge's): 0 not asleep, 1 asleep with unknown stage, 2 deep,
 * 3 light, 4 REM, 5 awake. Drivers map their own codes to these. */
enum {
    SALUS_SLEEP_STAGE_NOT_ASLEEP = 0,
    SALUS_SLEEP_STAGE_UNKNOWN = 1,
    SALUS_SLEEP_STAGE_DEEP = 2,
    SALUS_SLEEP_STAGE_LIGHT = 3,
    SALUS_SLEEP_STAGE_REM = 4,
    SALUS_SLEEP_STAGE_AWAKE = 5,
};

/* Stored value → timeline stage to paint, or -1 when the minute is not sleep. */
int salus_normalize_sleep_stage(int stage);

/* Some sources report stage lengths in seconds instead of minutes; these detect and fix that. */
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
