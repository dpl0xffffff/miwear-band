#ifndef MIWEAR_UI_HEALTH_DATA_H
#define MIWEAR_UI_HEALTH_DATA_H

#include "miwear/app.h"
#include <json-glib/json-glib.h>

/* Minute timeline filler when bed/wake exists but per-stage data does not (not deep/light/rem/awake). */
#define MIWEAR_SLEEP_STAGE_UNSPEC 1

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
} MiwearSleepDurationSummary;

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
    MiwearDailySummaryRow summary;
} MiwearDayHealthAgg;

void miwear_health_aggregate_day(AppState *state, const gchar *day_key, MiwearDayHealthAgg *out);

gboolean miwear_spo2_value_valid(int spo2);
gboolean miwear_stress_value_valid(int stress);
const char *miwear_pressure_level_label(int pressure);
guint miwear_apply_pressure_from_activity_record_json(AppState *state, JsonNode *decoded, uint32_t base_ts_sec);
guint miwear_standing_display_value(int standing_field);
guint miwear_activity_goal_default(void);
/* Minutes of @day_key at moderate or higher intensity, from synced per-minute data. */
guint miwear_count_activity_minutes_from_timeline(AppState *state, const gchar *day_key);
guint miwear_standing_hours_from_bitmap(int standing_raw);
guint miwear_count_moderate_activity_minutes_json(JsonNode *decoded);
float miwear_profile_bmi_kg_m2(float weight_kg, guint height_cm);
int miwear_hr_zone_index(int hr_bpm, int max_hr);
const char *miwear_hr_zone_label(int zone);
int miwear_cycle_day_of_period(GDate *today, guint last_period_yyyymmdd, guint cycle_length_days);
const char *miwear_cycle_phase_label(int cycle_day, guint cycle_length_days);

#endif
