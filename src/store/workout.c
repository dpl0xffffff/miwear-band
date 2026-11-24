#include "salus/store/workout.h"

#include <math.h>
#include <string.h>

static const struct {
    const char *key, *title, *unit;
} k_series[SALUS_SERIES_COUNT] = {
    [SALUS_SERIES_PACE] = {"pace", "Pace", "/km"},
    [SALUS_SERIES_SPEED] = {"speed", "Speed", "km/h"},
    [SALUS_SERIES_CADENCE] = {"cadence", "Cadence", "/min"},
    [SALUS_SERIES_STRIDE] = {"stride", "Stride", "cm"},
    [SALUS_SERIES_POWER] = {"power", "Power", "W"},
    [SALUS_SERIES_ALTITUDE] = {"altitude", "Altitude", "m"},
    [SALUS_SERIES_DISTANCE] = {"distance", "Distance", "m"},
    [SALUS_SERIES_CALORIES] = {"calories", "Calories", "kcal"},
    [SALUS_SERIES_SPO2] = {"spo2", "SpO₂", "%"},
    [SALUS_SERIES_STRESS] = {"stress", "Stress", ""},
    [SALUS_SERIES_SWOLF] = {"swolf", "SWOLF", ""},
    [SALUS_SERIES_STROKE_RATE] = {"stroke_rate", "Stroke rate", "/min"},
    [SALUS_SERIES_GROUND_CONTACT] = {"ground_contact", "Ground contact", "ms"},
    [SALUS_SERIES_VERTICAL_OSC] = {"vertical_osc", "Vertical oscillation", "cm"},
    [SALUS_SERIES_DEPTH] = {"depth", "Depth", "m"},
    [SALUS_SERIES_WATER_TEMP] = {"water_temp", "Water temperature", "°C"},
};

const char *salus_series_key(SalusRecordSeries s) {
    return (unsigned)s < SALUS_SERIES_COUNT ? k_series[s].key : NULL;
}

int salus_series_from_key(const char *key) {
    for (int i = 0; key != NULL && i < SALUS_SERIES_COUNT; i++) {
        if (strcmp(k_series[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}

const char *salus_series_title(SalusRecordSeries s) {
    return (unsigned)s < SALUS_SERIES_COUNT ? k_series[s].title : "";
}

const char *salus_series_unit(SalusRecordSeries s) {
    return (unsigned)s < SALUS_SERIES_COUNT ? k_series[s].unit : "";
}

SalusWorkoutSample salus_workout_sample_init(uint32_t offset_sec) {
    SalusWorkoutSample s = {.offset_sec = offset_sec};
    for (int i = 0; i < SALUS_SERIES_COUNT; i++) {
        s.v[i] = NAN;
    }
    return s;
}

void salus_workout_summary_free_series(SalusWorkoutSummary *s) {
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

const char *salus_swim_stroke_name(int main_stroke) {
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

void salus_format_walk_pace(int sec_per_km, gchar *buf, gsize buf_len) {
    if (buf == NULL || buf_len == 0) {
        return;
    }
    if (sec_per_km <= 0) {
        g_snprintf(buf, (gint)buf_len, "—");
        return;
    }
    g_snprintf(buf, (gint)buf_len, "%d'%02d''", sec_per_km / 60, sec_per_km % 60);
}

void salus_format_swim_pace(int sec_per_100m, gchar *buf, gsize buf_len) {
    if (buf == NULL || buf_len == 0) {
        return;
    }
    if (sec_per_100m <= 0) {
        g_snprintf(buf, (gint)buf_len, "—");
        return;
    }
    g_snprintf(buf, (gint)buf_len, "%d'%02d''", sec_per_100m / 60, sec_per_100m % 60);
}

void salus_format_recovery_duration(int recovery_min, gchar *buf, gsize buf_len) {
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

void salus_workout_summary_clear(SalusWorkoutSummary *s) {
    if (s == NULL) {
        return;
    }
    g_free(s->sport_name);
    salus_workout_summary_free_series(s);
    if (s->metrics != NULL) {
        g_array_unref(s->metrics);
    }
    memset(s, 0, sizeof(*s));
}

SalusWorkoutSummary *salus_workout_summary_dup(const SalusWorkoutSummary *src) {
    if (src == NULL) {
        return NULL;
    }
    SalusWorkoutSummary *d = g_new0(SalusWorkoutSummary, 1);
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

gboolean salus_workout_has_metric(const SalusWorkoutSummary *s, int id) {
    if (s == NULL || s->metrics == NULL) {
        return FALSE;
    }
    for (guint i = 0; i < s->metrics->len; i++) {
        if (g_array_index(s->metrics, SalusWorkoutMetric, i).id == id) {
            return TRUE;
        }
    }
    return FALSE;
}

double salus_workout_metric(const SalusWorkoutSummary *s, int id, double fallback) {
    if (s == NULL || s->metrics == NULL) {
        return fallback;
    }
    for (guint i = 0; i < s->metrics->len; i++) {
        const SalusWorkoutMetric *m = &g_array_index(s->metrics, SalusWorkoutMetric, i);
        if (m->id == id) {
            return m->value;
        }
    }
    return fallback;
}
