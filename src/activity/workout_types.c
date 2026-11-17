#include "miwear/activity/workout_types.h"
#include "miwear/activity/mi_fitness_report_tables.h"

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
    /* Same ids as Mi Fitness FitnessSportType: 1–26 are the band's file types, 100+ specific sports. */
    const char *name = mi_fitness_sport_type_name(subtype);
    return name != NULL ? name : "Workout";
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
    if (s->metrics != NULL) {
        g_array_unref(s->metrics);
    }
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
    d->metrics = src->metrics != NULL ? g_array_ref(src->metrics) : NULL;
    if (src->record_samples != NULL) {
        d->record_samples = g_array_ref(src->record_samples);
    }
    if (src->gps_track != NULL) {
        d->gps_track = g_array_ref(src->gps_track);
    }
    return d;
}

gboolean miwear_workout_has_metric(const MiwearWorkoutSummary *s, int id) {
    if (s == NULL || s->metrics == NULL) {
        return FALSE;
    }
    for (guint i = 0; i < s->metrics->len; i++) {
        if (g_array_index(s->metrics, MiwearWorkoutMetric, i).id == id) {
            return TRUE;
        }
    }
    return FALSE;
}

double miwear_workout_metric(const MiwearWorkoutSummary *s, int id, double fallback) {
    if (s == NULL || s->metrics == NULL) {
        return fallback;
    }
    for (guint i = 0; i < s->metrics->len; i++) {
        const MiwearWorkoutMetric *m = &g_array_index(s->metrics, MiwearWorkoutMetric, i);
        if (m->id == id) {
            return m->value;
        }
    }
    return fallback;
}
