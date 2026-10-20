#include "miwear/activity/workout_types.h"

#include <string.h>

const char *miwear_sport_subtype_name(int subtype) {
    /* Wire subtype uses flags>>2; Mi Fitness sportType is the same numeric id (e.g. 22 outdoor_foot). */
    switch (subtype) {
        case 1:
            return "Outdoor run";
        case 2:
            return "Outdoor walk";
        case 3:
            return "Treadmill";
        case 6:
            return "Outdoor cycling";
        case 7:
            return "Indoor cycling";
        case 8:
            return "Free training";
        case 9:
            return "Pool swim";
        case 11:
            return "Elliptical";
        case 13:
            return "Rowing";
        case 14:
            return "Jump rope";
        case 16:
            return "HIIT";
        case 22:
            return "Outdoor walk";
        case 23:
            return "Outdoor cycling";
        default:
            return "Workout";
    }
}

void miwear_workout_summary_clear(MiwearWorkoutSummary *s) {
    if (s == NULL) {
        return;
    }
    g_free(s->sport_name);
    memset(s, 0, sizeof(*s));
}
