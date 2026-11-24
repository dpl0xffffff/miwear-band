#ifndef SALUS_APP_H
#define SALUS_APP_H

#include <stdint.h>
#include <glib.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <adwaita.h>


#include "salus/util.h"
#include "salus/store/db.h"
#include "salus/device.h"

typedef struct SalusDashboardUi SalusDashboardUi;
typedef struct SalusConnectUi SalusConnectUi;

typedef struct SalusWorkoutSummary SalusWorkoutSummary;

typedef struct SalusSportEntry {
    uint32_t ts_sec;
    uint32_t start_sec;
    uint32_t end_sec;
    int sport_type;
    int sport_subtype;
    int duration_sec;
    int distance_m;
    int calories;
    int hr_avg;
    gchar *label;
    SalusWorkoutSummary *detail;
} SalusSportEntry;


typedef struct SalusApp {
    AdwApplication *app;
    GtkWidget *window;
    GtkWidget *stack;
    GtkWidget *page_dashboard;
    SalusDashboardUi *dash_ui;
    GDate *health_view_date;
    GList *sport_entries;
    GtkTextBuffer *log_buffer;
    GtkWidget *toast_overlay;
    GtkWidget *page_connect;
    SalusConnectUi *connect_ui;

    gchar *selected_address;
    gchar *device_display_name;
    gchar *device_status_wearing;
    gchar *device_status_charging;
    gchar *device_status_sleep;

    /* Saved devices (SalusDeviceConfig *), and the active one's driver session (NULL when none). */
    GPtrArray *saved_devices;
    SalusDevice *device;

    /*
     * Threading model: every field of SalusApp is owned by the GTK main thread; drivers do their
     * blocking work on worker threads and hand results back via the main loop.
     * Exceptions: append_log()/set_status()/set_connect_busy() are safe from any thread.
     */

    GHashTable *recorded_days;
    /* day key "YYYY-MM-DD" -> SalusDailySummaryRow*, the band's own per-day totals. */
    GHashTable *daily_summaries;
    guint recorded_rebuild_source_id;
    GMutex recorded_lock;
    GQueue *log_queue;
    GMutex log_lock;
    guint log_flush_id;
    /* Wake-day keys → duration totals from XIAOMI_SLEEP_TIME_SAMPLE (when synced). */
    GHashTable *sleep_duration_by_day;

    guint goal_moving;
    /* User profile (config.ini [profile] + sent to band as UserInfo). */
    guint profile_height_cm;
    float profile_weight_kg;
    guint profile_birthday_yyyymmdd;
    guint profile_gender;
    guint profile_max_hr;
    guint profile_goal_moving;
    guint profile_cycle_length_days;
    guint profile_last_period_yyyymmdd;
    GArray *weight_log;
    gboolean weight_log_loaded;
    /* Real-time stats from the band while connected. */
    guint live_steps;
    guint live_hr;
    gboolean has_live_steps;
    gboolean has_live_hr;

    gchar *weather_location_name;
    double weather_latitude;
    double weather_longitude;
    gboolean weather_coords_valid;
    gchar *weather_last_status;
    gboolean dashboard_refresh_pending;

    gboolean find_phone_active;

    gboolean notifications_enabled;
    gboolean share_location_with_band; /* send this computer's location during outdoor workouts */
    gchar *location_unavailable_reply;  /* "none" (default), "denied" or "ok": answer when there's no usable fix */
    guint notifications_min_interval_ms;
    guint32 notification_next_id;
    gint64 notification_last_sent_ms;
    gchar *notification_status;
    GDBusConnection *notification_bus;
    gulong notification_bus_filter_id;
    gulong notification_bus_outgoing_filter_id;
    gboolean notification_monitor_active;

    GCancellable *weather_cancellable;
    gboolean weather_fetch_inflight;
} SalusApp;

typedef struct {
    uint16_t minute_of_day;
    int steps;
    int active_calories;
    int distance_cm;
    int heart_rate;
    int energy;
    int spo2;
    int stress;
    int sleep_stage;
    int sleep_source;
    int intensity; /* per-minute activeStreng (3 moderate, 4 high, 5 intense); 0 when still or unknown */
} RecordedSample;

void append_log(SalusApp *state, const gchar *line);
void set_status(SalusApp *state, const gchar *status);
void set_connect_busy(SalusApp *state, gboolean busy);
void switch_to_dashboard(SalusApp *state);
void switch_to_pairing(SalusApp *state);
/* Values the band pushed while connected; unset fields are left as they were. Main thread. */
typedef struct {
    gboolean has_hr;
    guint hr;
    gboolean has_steps;
    guint steps;
    gboolean has_battery;
    guint battery;
} SalusLiveMetrics;
void salus_live_metrics_update(SalusApp *state, const SalusLiveMetrics *m);
void schedule_state_update(SalusApp *state, const gchar *charging, const gchar *wearing, const gchar *sleep);
void recorded_store_samples_for_day_key(SalusApp *state, const gchar *day_key, const RecordedSample *samples, size_t count);
/* Day-bucketed batch of per-minute sleep samples for the recorded timelines. */
typedef struct RecordedBatch RecordedBatch;
RecordedBatch *recorded_batch_new(void);
void recorded_batch_add_sleep_minute(RecordedBatch *batch, gint64 unix_sec, int stage);
/* Hands the samples to the timelines (merged on the next idle) and frees the batch. */
void recorded_batch_commit(SalusApp *state, RecordedBatch *batch);
void recorded_store_daily_details(SalusApp *state, uint32_t ts, const RecordedSample *samples, size_t count);
void recorded_merge_xiaomi_activity_rows(SalusApp *state, const SalusXiaomiActivityRow *rows, size_t count);
void health_apply_daily_summary(SalusApp *state, const SalusDailySummaryRow *row);
void recorded_rebuild_from_db(SalusApp *state);
/* Coalesced rebuild on the next idle (several requests before it runs → one rebuild). */
void recorded_schedule_rebuild(SalusApp *state);

gchar *format_hex(const uint8_t *data, size_t len);
gboolean looks_printable(const uint8_t *data, size_t len);
gboolean hex_to_bytes16(const char *hex, uint8_t out[16]);

gboolean bluez_pair_trust_connect(SalusApp *state, const gchar *address);
gchar *bluez_device_display_name(const gchar *address);


#endif /* SALUS_APP_H */
