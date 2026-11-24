#ifndef SALUS_ACTIVITY_WORKOUT_TYPES_H
#define SALUS_ACTIVITY_WORKOUT_TYPES_H

#include <stdint.h>
#include <glib.h>

typedef struct SalusWorkoutHrZones {
    int zone1_sec;
    int zone2_sec;
    int zone3_sec;
    int zone4_sec;
    int zone5_sec;
    int zone1_lower_bpm;
    int zone2_lower_bpm;
    int zone3_lower_bpm;
    int zone4_lower_bpm;
    int zone5_lower_bpm;
} SalusWorkoutHrZones;

/* Per-record series besides heart rate and steps (Mi Fitness OneSportRecord fields). */
typedef enum {
    SALUS_SERIES_PACE,          /* s/km */
    SALUS_SERIES_SPEED,         /* km/h (diving: signed vertical speed, unit not documented) */
    SALUS_SERIES_CADENCE,       /* steps/min, rpm (cycling), strokes/min (rowing), jumps/min */
    SALUS_SERIES_STRIDE,        /* cm */
    SALUS_SERIES_POWER,         /* W */
    SALUS_SERIES_ALTITUDE,      /* m */
    SALUS_SERIES_DISTANCE,      /* m in this record */
    SALUS_SERIES_CALORIES,      /* kcal in this record */
    SALUS_SERIES_SPO2,          /* % */
    SALUS_SERIES_STRESS,        /* 0–100 */
    SALUS_SERIES_SWOLF,
    SALUS_SERIES_STROKE_RATE,   /* strokes/min */
    SALUS_SERIES_GROUND_CONTACT,/* ms */
    SALUS_SERIES_VERTICAL_OSC,  /* cm */
    SALUS_SERIES_DEPTH,         /* m */
    SALUS_SERIES_WATER_TEMP,    /* °C */
    SALUS_SERIES_COUNT
} SalusRecordSeries;

typedef struct {
    uint32_t offset_sec; /* from the first record segment's start */
    uint8_t hr;          /* 0 = none */
    uint16_t steps_delta;
    float v[SALUS_SERIES_COUNT]; /* NAN = not recorded */
} SalusWorkoutSample;

/* Stable key of a series (persistence), and its display name and unit. */
const char *salus_series_key(SalusRecordSeries s);
int salus_series_from_key(const char *key); /* -1 if unknown */
const char *salus_series_title(SalusRecordSeries s);
const char *salus_series_unit(SalusRecordSeries s);
/* A sample with no extra series set. */
SalusWorkoutSample salus_workout_sample_init(uint32_t offset_sec);

typedef struct {
    double lat;
    double lon;
} SalusWorkoutGpsPoint;

/* One reported value, keyed by Mi Fitness report field id (SportReportBaseParser.ReportType). */
typedef struct {
    int id;
    double value;
} SalusWorkoutMetric;

typedef struct SalusWorkoutSummary {
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
    /* Specific sport (FitnessSportType, e.g. 1 outdoor run under file type 22); 0 = not reported. */
    int sub_sport_type;
    /* Every other valid report field (SalusWorkoutMetric), for per-sport details. NULL = none. */
    GArray *metrics;
    /* Pool swim (subtype 9/10) report fields from swimming_report_v8. */
    gboolean has_swim_detail;
    int swim_lengths;
    int swim_main_stroke;
    int swim_avg_pace_sec_per_100m;
    int swim_avg_swolf;
    int swim_total_strokes;
    int swim_avg_spm;
    int swim_training_load;
    int swim_recovery_min;
    int swim_vitality_points;
    int swim_pool_width_m;
    float swim_training_effect;
    SalusWorkoutHrZones hr_zones;
    /* Outdoor walk / step sports (subtype 2, 22, …). */
    gboolean has_walk_detail;
    int walk_steps;
    int walk_avg_pace_sec_per_km;
    float walk_avg_speed_kmh;
    int walk_step_length_cm;
    int walk_avg_cadence_spm;
    int walk_max_cadence_spm;
    float walk_te_aerobic;
    float walk_te_anaerobic;
    int walk_training_load;
    int walk_recovery_min;
    int walk_vitality_points;
    /* Training metrics shared by all sports (Mi Fitness report fields). 0 = not reported.
     * training_load_level: 1 too light, 2 optimal, 3 high, 4 overreaching.
     * training_status: 1 none, 2 overreaching, 3 detraining, 4 unproductive, 5 maintaining,
     *                  6 recovery, 7 productive, 8 peaking.
     * vo2_max in ml/kg/min; vo2_max_level: 1 novice … 7 elite. */
    int training_load;
    int training_load_level;
    int training_status;
    int vo2_max;
    int vo2_max_level;
    int recovery_min;
    GArray *record_samples;
    int record_interval_sec;
    GArray *gps_track;
} SalusWorkoutSummary;

const char *salus_sport_subtype_name(int subtype);
const char *salus_swim_stroke_name(int main_stroke);
void salus_format_swim_pace(int sec_per_100m, gchar *buf, gsize buf_len);
void salus_format_walk_pace(int sec_per_km, gchar *buf, gsize buf_len);
void salus_format_recovery_duration(int recovery_min, gchar *buf, gsize buf_len);

void salus_workout_summary_free_series(SalusWorkoutSummary *s);
void salus_workout_summary_clear(SalusWorkoutSummary *s);
SalusWorkoutSummary *salus_workout_summary_dup(const SalusWorkoutSummary *src);
/* Value of report field `id` from `metrics`, or `fallback` when absent. */
double salus_workout_metric(const SalusWorkoutSummary *s, int id, double fallback);
gboolean salus_workout_has_metric(const SalusWorkoutSummary *s, int id);

#endif
