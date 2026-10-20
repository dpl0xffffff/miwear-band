#ifndef MIWEAR_ACTIVITY_WORKOUT_TYPES_H
#define MIWEAR_ACTIVITY_WORKOUT_TYPES_H

#include <stdint.h>
#include <glib.h>

typedef struct MiwearWorkoutSummary {
    uint32_t file_ts_sec;
    uint32_t start_sec;
    uint32_t end_sec;
    int duration_sec;
    int distance_m;
    int calories;
    int hr_avg;
    int hr_max;
    int hr_min;
    int sport_subtype;
    int version;
    gchar *sport_name;
} MiwearWorkoutSummary;

const char *miwear_sport_subtype_name(int subtype);

void miwear_workout_summary_clear(MiwearWorkoutSummary *s);

#endif
