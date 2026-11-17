#include "miwear/activity/workout_types.h"

#include <string.h>

void miwear_workout_summary_free_series(MiwearWorkoutSummary *s) {
    if (s == NULL) {
        return;
    }
    if (s->record_samples != NULL) {
        g_array_unref(s->record_samples);
        s->record_samples = NULL;
    }
    if (s->gps_track != NULL) {
        g_array_unref(s->gps_track);
        s->gps_track = NULL;
    }
    s->record_interval_sec = 0;
}

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

const char *miwear_swim_stroke_name(int main_stroke) {
    switch (main_stroke) {
        case 0:
            return "Mixed";
        case 1:
            return "Breaststroke";
        case 2:
            return "Freestyle";
        case 3:
            return "Backstroke";
        case 4:
            return "Butterfly";
        default:
            return "Unknown";
    }
}

void miwear_format_walk_pace(int sec_per_km, gchar *buf, gsize buf_len) {
    if (buf == NULL || buf_len == 0) {
        return;
    }
    if (sec_per_km <= 0) {
        g_snprintf(buf, (gint)buf_len, "—");
        return;
    }
    g_snprintf(buf, (gint)buf_len, "%d'%02d''", sec_per_km / 60, sec_per_km % 60);
}

void miwear_format_swim_pace(int sec_per_100m, gchar *buf, gsize buf_len) {
    if (buf == NULL || buf_len == 0) {
        return;
    }
    if (sec_per_100m <= 0) {
        g_snprintf(buf, (gint)buf_len, "—");
        return;
    }
    g_snprintf(buf, (gint)buf_len, "%d'%02d''", sec_per_100m / 60, sec_per_100m % 60);
}

void miwear_format_recovery_duration(int recovery_min, gchar *buf, gsize buf_len) {
    if (buf == NULL || buf_len == 0) {
        return;
    }
    if (recovery_min <= 0) {
        g_snprintf(buf, (gint)buf_len, "—");
        return;
    }
    if (recovery_min >= 60 && recovery_min % 60 == 0) {
        g_snprintf(buf, (gint)buf_len, "%dh", recovery_min / 60);
        return;
    }
    if (recovery_min >= 60) {
        g_snprintf(buf, (gint)buf_len, "%dh %dm", recovery_min / 60, recovery_min % 60);
        return;
    }
    g_snprintf(buf, (gint)buf_len, "%d min", recovery_min);
}

void miwear_workout_summary_clear(MiwearWorkoutSummary *s) {
    if (s == NULL) {
        return;
    }
    g_free(s->sport_name);
    miwear_workout_summary_free_series(s);
    memset(s, 0, sizeof(*s));
}

MiwearWorkoutSummary *miwear_workout_summary_dup(const MiwearWorkoutSummary *src) {
    if (src == NULL) {
        return NULL;
    }
    MiwearWorkoutSummary *d = g_new0(MiwearWorkoutSummary, 1);
    *d = *src;
    d->sport_name = g_strdup(src->sport_name);
    d->record_samples = NULL;
    d->gps_track = NULL;
    if (src->record_samples != NULL) {
        d->record_samples = g_array_ref(src->record_samples);
    }
    if (src->gps_track != NULL) {
        d->gps_track = g_array_ref(src->gps_track);
    }
    return d;
}
