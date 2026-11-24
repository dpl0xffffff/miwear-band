/* SQLite persistence aligned with Gadgetbridge Xiaomi schema (GreenDAO table/column names). */

#ifndef SALUS_DB_H
#define SALUS_DB_H

#include <glib.h>
#include <stddef.h>
#include <stdint.h>

#include "salus/activity/workout_types.h"

typedef struct {
    int32_t timestamp_sec;
    int raw_intensity;
    int steps;
    int raw_kind;
    int heart_rate;
    int stress;
    int spo2;
    int distance_cm;
    int active_calories;
    int energy;
} SalusXiaomiActivityRow;

typedef struct {
    int64_t timestamp_ms;
    int timezone;
    int steps;
    int hr_resting;
    int hr_max;
    int32_t hr_max_ts_sec;
    int hr_min;
    int32_t hr_min_ts_sec;
    int hr_avg;
    int stress_avg;
    int stress_max;
    int stress_min;
    int standing_bits;
    int calories;            /* totalCalories */
    gboolean has_active_calories;
    int active_calories;     /* totalactiveCalories: the value the band shows */
    int spo2_max;
    int32_t spo2_max_ts_sec;
    int spo2_min;
    int32_t spo2_min_ts_sec;
    int spo2_avg;
    int training_load_day;
    int training_load_week;
    int training_load_level;
    int vitality_increase_light;
    int vitality_increase_moderate;
    int vitality_increase_high;
    int vitality_current;
    gboolean has_training_vitality;
} SalusDailySummaryRow;

typedef struct {
    int64_t timestamp_ms;
    int stage;
} SalusSleepStageRow;

typedef struct {
    int64_t bed_ms;
    int64_t wake_ms;
    int total_min;
    int deep_min;
    int light_min;
    int rem_min;
    int awake_min;
} SalusSleepTimeRow;

typedef struct {
    int64_t timestamp_ms;
} SalusHeartPulseRow;

typedef struct {
    uint32_t t_sec;
    uint8_t raw_stage;
} GbRawSleepPhaseChange;

gboolean salus_db_open(void);
void salus_db_close(void);

/* Register paired device (Bluetooth address). Required before inserts; safe from any thread. */
gboolean salus_db_ensure_device(const char *bluetooth_address);

gboolean salus_db_replace_xiaomi_activity_batch(const SalusXiaomiActivityRow *rows, size_t n);

gboolean salus_db_replace_daily_summary(const SalusDailySummaryRow *row);

/* Sleep stages file (v2): raw phase bytes as in Gadgetbridge SleepStagesParser. */
gboolean salus_db_replace_sleep_stages_v2(uint32_t bed_sec,
                                       uint32_t wake_sec,
                                       uint16_t total_min,
                                       uint16_t deep_min,
                                       uint16_t light_min,
                                       uint16_t rem_min,
                                       uint16_t awake_min,
                                       const GbRawSleepPhaseChange *phases,
                                       size_t n_phases);

/* Sleep details: optional summary durations (use -1 for unknown int fields). */
gboolean salus_db_replace_sleep_details_header(int64_t bed_ms,
                                            int64_t wake_ms,
                                            int is_awake,
                                            int total_min,
                                            int deep_min,
                                            int light_min,
                                            int rem_min,
                                            int awake_min);

gboolean salus_db_replace_sleep_stage_samples(const SalusSleepStageRow *rows, size_t n);
gboolean salus_db_replace_heart_pulse_samples(const SalusHeartPulseRow *rows, size_t n);

typedef gboolean (*SalusDbActivityRowFn)(const SalusXiaomiActivityRow *row, gpointer user_data);
typedef gboolean (*SalusDbSleepStageRowFn)(const SalusSleepStageRow *row, gpointer user_data);
typedef gboolean (*SalusDbSleepTimeRowFn)(const SalusSleepTimeRow *row, gpointer user_data);
typedef gboolean (*SalusDbDailySummaryRowFn)(const SalusDailySummaryRow *row, gpointer user_data);

gboolean salus_db_foreach_xiaomi_activity(SalusDbActivityRowFn fn, gpointer user_data);

gboolean salus_db_foreach_sleep_stage(SalusDbSleepStageRowFn fn, gpointer user_data);
gboolean salus_db_foreach_sleep_time(SalusDbSleepTimeRowFn fn, gpointer user_data);
gboolean salus_db_foreach_daily_summary(SalusDbDailySummaryRowFn fn, gpointer user_data);

typedef struct {
    uint32_t file_ts;
    int sport_type;
    int sport_subtype;
    gchar *detail_json;
} SalusDbSportWorkoutRow;

typedef gboolean (*SalusDbSportWorkoutRowFn)(const SalusDbSportWorkoutRow *row, gpointer user_data);

gboolean salus_db_replace_sport_workout(int sport_type,
                                         uint32_t file_ts,
                                         int sport_subtype,
                                         const SalusWorkoutSummary *summary);
gboolean salus_db_foreach_sport_workout(SalusDbSportWorkoutRowFn fn, gpointer user_data);

#endif
