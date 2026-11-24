#include "salus/store/sleep.h"

#include <stddef.h>

int salus_normalize_sleep_stage(int stage) {
    if (stage >= SALUS_SLEEP_STAGE_UNKNOWN && stage <= SALUS_SLEEP_STAGE_AWAKE) {
        return stage;
    }
    return -1; /* not asleep, or not a valid stage */
}

static int positive(int v) {
    return v > 0 ? v : 0;
}

static int looks_like_seconds_not_minutes(int deep, int light, int rem, int awake, int total) {
    const int sum = positive(deep) + positive(light) + positive(rem) + positive(awake);
    if (sum > SALUS_SLEEP_MAX_PLAUSIBLE_MINUTES) {
        return 1;
    }
    if (positive(deep) > SALUS_SLEEP_MAX_PLAUSIBLE_STAGE_MINUTES ||
        positive(light) > SALUS_SLEEP_MAX_PLAUSIBLE_STAGE_MINUTES ||
        positive(rem) > SALUS_SLEEP_MAX_PLAUSIBLE_STAGE_MINUTES ||
        positive(awake) > SALUS_SLEEP_MAX_PLAUSIBLE_STAGE_MINUTES) {
        return 1;
    }
    if (positive(total) > SALUS_SLEEP_MAX_PLAUSIBLE_MINUTES) {
        return 1;
    }
    return 0;
}

int salus_sleep_clamp_stage_minutes(int minutes) {
    if (minutes < 0) {
        return minutes;
    }
    if (minutes == 0) {
        return 0;
    }
    if (minutes > SALUS_SLEEP_MAX_PLAUSIBLE_MINUTES) {
        return SALUS_SLEEP_MAX_PLAUSIBLE_MINUTES;
    }
    return minutes;
}

void salus_sleep_normalize_stage_durations(SalusSleepStageDurations *d) {
    if (d == NULL) {
        return;
    }
    if (!looks_like_seconds_not_minutes(d->deep_min, d->light_min, d->rem_min, d->awake_min, d->total_min)) {
        d->deep_min = salus_sleep_clamp_stage_minutes(d->deep_min);
        d->light_min = salus_sleep_clamp_stage_minutes(d->light_min);
        d->rem_min = salus_sleep_clamp_stage_minutes(d->rem_min);
        d->awake_min = salus_sleep_clamp_stage_minutes(d->awake_min);
        d->total_min = salus_sleep_clamp_stage_minutes(d->total_min);
        return;
    }
    if (d->deep_min > 0) {
        d->deep_min = salus_sleep_clamp_stage_minutes((d->deep_min + 59) / 60);
    }
    if (d->light_min > 0) {
        d->light_min = salus_sleep_clamp_stage_minutes((d->light_min + 59) / 60);
    }
    if (d->rem_min > 0) {
        d->rem_min = salus_sleep_clamp_stage_minutes((d->rem_min + 59) / 60);
    }
    if (d->awake_min > 0) {
        d->awake_min = salus_sleep_clamp_stage_minutes((d->awake_min + 59) / 60);
    }
    if (d->total_min > SALUS_SLEEP_MAX_PLAUSIBLE_MINUTES) {
        d->total_min = salus_sleep_clamp_stage_minutes((d->total_min + 59) / 60);
    }
}
