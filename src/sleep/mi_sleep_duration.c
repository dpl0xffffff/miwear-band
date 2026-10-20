#include "miwear/sleep/mi_sleep_duration.h"

#include <stddef.h>

static int positive(int v) {
    return v > 0 ? v : 0;
}

static int looks_like_seconds_not_minutes(int deep, int light, int rem, int awake, int total) {
    const int sum = positive(deep) + positive(light) + positive(rem) + positive(awake);
    if (sum > MIWEAR_SLEEP_MAX_PLAUSIBLE_MINUTES) {
        return 1;
    }
    if (positive(deep) > MIWEAR_SLEEP_MAX_PLAUSIBLE_STAGE_MINUTES ||
        positive(light) > MIWEAR_SLEEP_MAX_PLAUSIBLE_STAGE_MINUTES ||
        positive(rem) > MIWEAR_SLEEP_MAX_PLAUSIBLE_STAGE_MINUTES ||
        positive(awake) > MIWEAR_SLEEP_MAX_PLAUSIBLE_STAGE_MINUTES) {
        return 1;
    }
    if (positive(total) > MIWEAR_SLEEP_MAX_PLAUSIBLE_MINUTES) {
        return 1;
    }
    return 0;
}

int miwear_sleep_clamp_stage_minutes(int minutes) {
    if (minutes < 0) {
        return minutes;
    }
    if (minutes == 0) {
        return 0;
    }
    if (minutes > MIWEAR_SLEEP_MAX_PLAUSIBLE_MINUTES) {
        return MIWEAR_SLEEP_MAX_PLAUSIBLE_MINUTES;
    }
    return minutes;
}

void miwear_sleep_normalize_stage_durations(MiwearSleepStageDurations *d) {
    if (d == NULL) {
        return;
    }
    if (!looks_like_seconds_not_minutes(d->deep_min, d->light_min, d->rem_min, d->awake_min, d->total_min)) {
        d->deep_min = miwear_sleep_clamp_stage_minutes(d->deep_min);
        d->light_min = miwear_sleep_clamp_stage_minutes(d->light_min);
        d->rem_min = miwear_sleep_clamp_stage_minutes(d->rem_min);
        d->awake_min = miwear_sleep_clamp_stage_minutes(d->awake_min);
        d->total_min = miwear_sleep_clamp_stage_minutes(d->total_min);
        return;
    }
    if (d->deep_min > 0) {
        d->deep_min = miwear_sleep_clamp_stage_minutes((d->deep_min + 59) / 60);
    }
    if (d->light_min > 0) {
        d->light_min = miwear_sleep_clamp_stage_minutes((d->light_min + 59) / 60);
    }
    if (d->rem_min > 0) {
        d->rem_min = miwear_sleep_clamp_stage_minutes((d->rem_min + 59) / 60);
    }
    if (d->awake_min > 0) {
        d->awake_min = miwear_sleep_clamp_stage_minutes((d->awake_min + 59) / 60);
    }
    if (d->total_min > MIWEAR_SLEEP_MAX_PLAUSIBLE_MINUTES) {
        d->total_min = miwear_sleep_clamp_stage_minutes((d->total_min + 59) / 60);
    }
}
