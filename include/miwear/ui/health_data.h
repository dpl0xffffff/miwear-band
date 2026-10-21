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
} MiwearDayHealthAgg;

void miwear_health_aggregate_day(AppState *state, const gchar *day_key, MiwearDayHealthAgg *out);
int miwear_health_sleep_total_minutes(const MiwearDayHealthAgg *agg);
gboolean miwear_health_sleep_has_stage_breakdown(const MiwearDayHealthAgg *agg);

gboolean miwear_spo2_value_valid(int spo2);
gboolean miwear_stress_value_valid(int stress);
int miwear_latest_stress_for_day(AppState *state, const gchar *day_key);
/* Picks newest stress by unix timestamp (DB + live cache + timeline). Logs source when state non-null. */
int miwear_display_stress_for_day(AppState *state, const gchar *day_key, const char **source_out);
const char *miwear_pressure_level_label(int pressure);
guint miwear_apply_pressure_from_activity_record_json(AppState *state, JsonNode *decoded, uint32_t base_ts_sec);
guint miwear_standing_display_value(int standing_field);
void miwear_format_standing_value(guint value, gchar *buf, gsize buf_len);
void miwear_format_activity_minutes(guint minutes, guint goal_minutes, gchar *buf, gsize buf_len);
guint miwear_activity_goal_default(void);
guint miwear_activity_time_from_realtime(gboolean has_unknown3, guint unknown3,
                                         gboolean has_unknown5, guint unknown5,
                                         gboolean has_steps, guint steps);
guint miwear_live_activity_minutes(const AppState *state);
guint miwear_display_activity_minutes(const AppState *state, const gchar *day_key, gboolean prefer_live);
guint miwear_count_activity_minutes_from_timeline(AppState *state, const gchar *day_key);
void miwear_refresh_activity_minutes_for_day(AppState *state, const gchar *day_key);
guint miwear_standing_hours_from_bitmap(int standing_raw);
guint miwear_count_moderate_activity_minutes_json(JsonNode *decoded);
float miwear_profile_bmi_kg_m2(float weight_kg, guint height_cm);
int miwear_hr_zone_index(int hr_bpm, int max_hr);
const char *miwear_hr_zone_label(int zone);
int miwear_cycle_day_of_period(GDate *today, guint last_period_yyyymmdd, guint cycle_length_days);
const char *miwear_cycle_phase_label(int cycle_day, guint cycle_length_days);

#endif
