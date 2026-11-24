#ifndef SALUS_UI_HEALTH_DATA_H
#define SALUS_UI_HEALTH_DATA_H

#include "salus/app.h"
#include <json-glib/json-glib.h>

/* Minute timeline filler when bed/wake exists but per-stage data does not (not deep/light/rem/awake). */
#include "salus/store/sleep.h"

typedef struct {
    int deep;
    int light;
    int rem;
    int awake;
    /* Band totalDuration (asleep), not bed→wake span. */
    int total_asleep;
    gboolean from_db;
    /* Earliest bed / latest wake of the session(s) ending on this day (unix seconds, 0 = unknown). */
    gint64 bed_sec;
    gint64 wake_sec;
} SalusSleepDurationSummary;

typedef struct {
    const gchar *day_key;
    int steps;
    int calories;
    int hr_sum;
    int hr_n;
    int hr_max;
    int spo2_sum;
    int spo2_n;
    int stress_sum;
    int stress_n;
    int sleep_deep;
    int sleep_light;
    int sleep_rem;
    int sleep_awake;
    int sleep_other;
    int sleep_total_band;
    gboolean has_sleep;
    /* The band's own totals for this day (XIAOMI_DAILY_SUMMARY_SAMPLE), when synced. */
    gboolean has_summary;
    SalusDailySummaryRow summary;
} SalusDayHealthAgg;

void salus_health_aggregate_day(SalusApp *state, const gchar *day_key, SalusDayHealthAgg *out);

gboolean salus_spo2_value_valid(int spo2);
gboolean salus_stress_value_valid(int stress);
const char *salus_pressure_level_label(int pressure);
guint salus_apply_pressure_from_activity_record_json(SalusApp *state, JsonNode *decoded, uint32_t base_ts_sec);
guint salus_standing_display_value(int standing_field);
guint salus_activity_goal_default(void);
/* Minutes of @day_key at moderate or higher intensity, from synced per-minute data. */
guint salus_count_activity_minutes_from_timeline(SalusApp *state, const gchar *day_key);
guint salus_standing_hours_from_bitmap(int standing_raw);
guint salus_count_moderate_activity_minutes_json(JsonNode *decoded);
float salus_profile_bmi_kg_m2(float weight_kg, guint height_cm);
int salus_hr_zone_index(int hr_bpm, int max_hr);
const char *salus_hr_zone_label(int zone);
int salus_cycle_day_of_period(GDate *today, guint last_period_yyyymmdd, guint cycle_length_days);
const char *salus_cycle_phase_label(int cycle_day, guint cycle_length_days);

#endif
