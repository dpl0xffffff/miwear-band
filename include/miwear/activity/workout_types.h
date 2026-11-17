#ifndef MIWEAR_ACTIVITY_WORKOUT_TYPES_H
#define MIWEAR_ACTIVITY_WORKOUT_TYPES_H

#include <stdint.h>
#include <glib.h>

typedef struct MiwearWorkoutHrZones {
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
} MiwearWorkoutHrZones;

typedef struct {
    uint32_t offset_sec;
    uint8_t hr;
    uint16_t steps_delta;
} MiwearWorkoutSample;

typedef struct {
    double lat;
    double lon;
} MiwearWorkoutGpsPoint;

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
    MiwearWorkoutHrZones hr_zones;
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
    GArray *record_samples;
    int record_interval_sec;
    GArray *gps_track;
} MiwearWorkoutSummary;

const char *miwear_sport_subtype_name(int subtype);
const char *miwear_swim_stroke_name(int main_stroke);
void miwear_format_swim_pace(int sec_per_100m, gchar *buf, gsize buf_len);
void miwear_format_walk_pace(int sec_per_km, gchar *buf, gsize buf_len);
void miwear_format_recovery_duration(int recovery_min, gchar *buf, gsize buf_len);

void miwear_workout_summary_free_series(MiwearWorkoutSummary *s);
void miwear_workout_summary_clear(MiwearWorkoutSummary *s);
MiwearWorkoutSummary *miwear_workout_summary_dup(const MiwearWorkoutSummary *src);

#endif
