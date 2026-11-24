#include "salus/app.h"
#include "salus/config.h"
#include "salus/ui/ui.h"
#include "salus/store/workout.h"
#include "salus/services/notifications.h"
#include "salus/services/find_phone.h"
#include "salus/ui/dashboard.h"
#include "ui_private.h"
#include "salus/store/health_data.h"
#include "salus/store/sleep.h"
#include "salus/store/db.h"
#include "salus/local_time.h"
#include "salus/services/weather.h"

#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <cairo.h>
#include <gio/gio.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <adwaita.h>

#include "xiaomi.pb-c.h"

typedef struct {
    SalusApp *state;
    gchar *status;
} UiStatusLine;

typedef struct {
    SalusApp *state;
    gchar *charging_text;
    gchar *wearing_text;
    gchar *sleep_text;
} UiStateUpdate;

typedef struct {
    SalusApp *state;
    gboolean busy;
} UiBusyState;

static void ui_set_sensitive_if_widget(GtkWidget *widget, gboolean sensitive) {
    if (widget != NULL && GTK_IS_WIDGET(widget)) {
        gtk_widget_set_sensitive(widget, sensitive);
    }
}

gboolean salus_ui_has_saved_device(SalusApp *state) {
    return state != NULL && state->saved_devices != NULL && state->saved_devices->len > 0;
}

void salus_ui_show_setup(SalusApp *state) {
    salus_connect_show(state);
}

#define APP_LOG_MAX_CHARS 300000

static gboolean ui_flush_log_queue_idle(gpointer user_data) {
    SalusApp *state = user_data;
    g_mutex_lock(&state->log_lock);
    GQueue *lines = state->log_queue;
    state->log_queue = NULL;
    state->log_flush_id = 0;
    g_mutex_unlock(&state->log_lock);

    GString *chunk = g_string_new(NULL);
    for (GList *l = lines != NULL ? lines->head : NULL; l != NULL; l = l->next) {
        g_string_append(chunk, l->data);
        g_string_append_c(chunk, '\n');
    }
    if (lines != NULL) {
        g_queue_free_full(lines, g_free);
    }

    if (chunk->len > 0 && state->log_buffer != NULL) {
        GtkTextIter end;
        gtk_text_buffer_begin_user_action(state->log_buffer);
        gtk_text_buffer_get_end_iter(state->log_buffer, &end);
        gtk_text_buffer_insert(state->log_buffer, &end, chunk->str, (gint)chunk->len);
        gint nchars = gtk_text_buffer_get_char_count(state->log_buffer);
        if (nchars > APP_LOG_MAX_CHARS) {
            GtkTextIter start, cut;
            gtk_text_buffer_get_start_iter(state->log_buffer, &start);
            gtk_text_buffer_get_iter_at_offset(state->log_buffer, &cut, nchars - APP_LOG_MAX_CHARS);
            gtk_text_buffer_delete(state->log_buffer, &start, &cut);
        }
        gtk_text_buffer_end_user_action(state->log_buffer);
    }
    g_string_free(chunk, TRUE);
    return G_SOURCE_REMOVE;
}

void append_log(SalusApp *state, const gchar *line) {
    if (line == NULL) return;
    g_mutex_lock(&state->log_lock);
    if (state->log_queue == NULL) {
        state->log_queue = g_queue_new();
    }
    g_queue_push_tail(state->log_queue, g_strdup(line));
    if (state->log_flush_id == 0) {
        state->log_flush_id = g_idle_add(ui_flush_log_queue_idle, state);
    }
    g_mutex_unlock(&state->log_lock);
}

static gboolean ui_set_status_cb(gpointer user_data) {
    UiStatusLine *ctx = user_data;
    salus_connect_set_status(ctx->state, ctx->status);
    salus_dashboard_set_status(ctx->state, ctx->status);
    g_free(ctx->status);
    g_free(ctx);
    return G_SOURCE_REMOVE;
}

static gboolean ui_set_busy_cb(gpointer user_data) {
    UiBusyState *ctx = user_data;
    salus_connect_set_busy(ctx->state, ctx->busy);
    salus_dashboard_set_busy(ctx->state, ctx->busy);
    g_free(ctx);
    return G_SOURCE_REMOVE;
}

void set_connect_busy(SalusApp *state, gboolean busy) {
    UiBusyState *ctx = g_new0(UiBusyState, 1);
    ctx->state = state;
    ctx->busy = busy;
    g_idle_add(ui_set_busy_cb, ctx);
}

void set_status(SalusApp *state, const gchar *status) {
    UiStatusLine *ctx = g_new0(UiStatusLine, 1);
    ctx->state = state;
    ctx->status = g_strdup(status);
    g_idle_add(ui_set_status_cb, ctx);
}

void salus_live_metrics_update(SalusApp *state, const SalusLiveMetrics *m) {
    if (m->has_hr) {
        state->live_hr = m->hr;
        state->has_live_hr = TRUE;
    }
    if (m->has_steps) {
        state->live_steps = m->steps;
        state->has_live_steps = TRUE;
    }
    if (m->has_battery && state->dash_ui != NULL) {
        state->dash_ui->battery = (int)m->battery;
    }
    salus_dashboard_schedule_refresh(state);
}

static gboolean ui_update_state_cb(gpointer user_data) {
    UiStateUpdate *u = user_data;
    salus_dashboard_update_device_state(u->state, u->charging_text, u->wearing_text, u->sleep_text);
    g_free(u->charging_text);
    g_free(u->wearing_text);
    g_free(u->sleep_text);
    g_free(u);
    return G_SOURCE_REMOVE;
}

void schedule_state_update(SalusApp *state, const gchar *charging, const gchar *wearing, const gchar *sleep) {
    UiStateUpdate *u = g_new0(UiStateUpdate, 1);
    u->state = state;
    u->charging_text = charging ? g_strdup(charging) : NULL;
    u->wearing_text = wearing ? g_strdup(wearing) : NULL;
    u->sleep_text = sleep ? g_strdup(sleep) : NULL;
    g_idle_add(ui_update_state_cb, u);
}


static void recorded_day_value_free(gpointer value) {
    if (value != NULL) {
        g_array_unref((GArray *)value);
    }
}

static GArray *recorded_create_day_array(void) {
    GArray *arr = g_array_sized_new(FALSE, FALSE, sizeof(RecordedSample), 1440);
    for (guint m = 0; m < 1440; m++) {
        RecordedSample s = {0};
        s.minute_of_day = (uint16_t)m;
        s.steps = -1;
        s.active_calories = -1;
        s.distance_cm = -1;
        s.heart_rate = -1;
        s.energy = -1;
        s.spo2 = -1;
        s.stress = -1;
        s.sleep_stage = -1;
        s.sleep_source = 0;
        g_array_append_val(arr, s);
    }
    return arr;
}

static void recorded_refresh_ui(SalusApp *state) {
    salus_dashboard_redraw_charts(state);
}

typedef struct {
    SalusApp *state;
    gchar *day_key;
    GArray *samples;
} RecordedStoreCtx;

static gboolean ui_recorded_store_cb(gpointer user_data) {
    RecordedStoreCtx *ctx = user_data;
    SalusApp *state = ctx->state;

    g_mutex_lock(&state->recorded_lock);
    if (state->recorded_days == NULL) {
        state->recorded_days = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, recorded_day_value_free);
    }
    GArray *day = g_hash_table_lookup(state->recorded_days, ctx->day_key);
    if (day == NULL) {
        day = recorded_create_day_array();
        g_hash_table_insert(state->recorded_days, g_strdup(ctx->day_key), day);
    }
    for (guint i = 0; i < ctx->samples->len; i++) {
        const RecordedSample *src = &g_array_index(ctx->samples, RecordedSample, i);
        const guint idx = src->minute_of_day;
        if (idx >= day->len) continue;
        RecordedSample *dst = &g_array_index(day, RecordedSample, idx);
        if (src->steps >= 0) dst->steps = src->steps;
        if (src->active_calories >= 0) dst->active_calories = src->active_calories;
        if (src->distance_cm >= 0) dst->distance_cm = src->distance_cm;
        if (src->heart_rate >= 0) dst->heart_rate = src->heart_rate;
        if (src->energy >= 0) dst->energy = src->energy;
        if (src->spo2 >= 0) dst->spo2 = src->spo2;
        if (src->stress >= 0) dst->stress = src->stress;
        if (src->sleep_stage >= 0) dst->sleep_stage = src->sleep_stage;
        if (src->sleep_source > 0) dst->sleep_source = 1;
        if (src->intensity > 0) dst->intensity = src->intensity;
    }
    g_mutex_unlock(&state->recorded_lock);

    recorded_refresh_ui(state);
    g_array_unref(ctx->samples);
    g_free(ctx->day_key);
    g_free(ctx);
    return G_SOURCE_REMOVE;
}

void recorded_store_samples_for_day_key(SalusApp *state, const gchar *day_key, const RecordedSample *samples, size_t count) {
    if (samples == NULL || count == 0 || day_key == NULL) return;
    GArray *arr = g_array_sized_new(FALSE, FALSE, sizeof(RecordedSample), (guint)count);
    g_array_append_vals(arr, samples, (guint)count);
    RecordedStoreCtx *ctx = g_new0(RecordedStoreCtx, 1);
    ctx->state = state;
    ctx->day_key = g_strdup(day_key);
    ctx->samples = arr;
    g_idle_add(ui_recorded_store_cb, ctx);
}

void recorded_store_daily_details(SalusApp *state, uint32_t ts, const RecordedSample *samples, size_t count) {
    if (samples == NULL || count == 0) return;
    gchar *day_key = salus_local_day_key((gint64)ts);
    if (day_key == NULL) return;
    recorded_store_samples_for_day_key(state, day_key, samples, count);
    g_free(day_key);
}

static void flush_recorded_day_buckets(SalusApp *state, GHashTable *by_day) {
    GHashTableIter it;
    gpointer k, v;
    g_hash_table_iter_init(&it, by_day);
    while (g_hash_table_iter_next(&it, &k, &v)) {
        GArray *bucket = v;
        if (bucket != NULL && bucket->len > 0) {
            recorded_store_samples_for_day_key(state, (const gchar *)k,
                                               (const RecordedSample *)bucket->data, bucket->len);
        }
    }
}

struct RecordedBatch {
    GHashTable *by_day; /* day key → GArray<RecordedSample> */
    SalusLocalDayCursor cursor;
};

RecordedBatch *recorded_batch_new(void) {
    RecordedBatch *b = g_new0(RecordedBatch, 1);
    b->by_day = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_array_unref);
    salus_local_day_cursor_init(&b->cursor);
    return b;
}

void recorded_batch_add_sleep_minute(RecordedBatch *b, gint64 unix_sec, int stage) {
    const gchar *day_key = NULL;
    guint16 mod = 0;
    if (b == NULL || !salus_local_day_cursor_resolve(&b->cursor, unix_sec, &day_key, &mod)) {
        return;
    }
    GArray *bucket = g_hash_table_lookup(b->by_day, day_key);
    if (bucket == NULL) {
        bucket = g_array_new(FALSE, FALSE, sizeof(RecordedSample));
        g_hash_table_insert(b->by_day, g_strdup(day_key), bucket);
    }
    RecordedSample s = {0};
    s.minute_of_day = mod;
    s.steps = s.active_calories = s.distance_cm = s.heart_rate = s.energy = s.spo2 = s.stress = -1;
    s.sleep_stage = stage;
    s.sleep_source = 1;
    g_array_append_val(bucket, s);
}

void recorded_batch_commit(SalusApp *state, RecordedBatch *b) {
    if (b == NULL) {
        return;
    }
    flush_recorded_day_buckets(state, b->by_day);
    g_hash_table_unref(b->by_day);
    salus_local_day_cursor_clear(&b->cursor);
    g_free(b);
}

void recorded_merge_xiaomi_activity_rows(SalusApp *state, const SalusXiaomiActivityRow *rows, size_t count) {
    if (state == NULL || rows == NULL || count == 0) {
        return;
    }
    GHashTable *by_day = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_array_unref);
    SalusLocalDayCursor cursor;
    salus_local_day_cursor_init(&cursor);
    for (size_t i = 0; i < count; i++) {
        const SalusXiaomiActivityRow *r = &rows[i];
        const gchar *day_key = NULL;
        guint16 mod = 0;
        if (!salus_local_day_cursor_resolve(&cursor, (gint64)r->timestamp_sec, &day_key, &mod)) {
            continue;
        }

        GArray *bucket = g_hash_table_lookup(by_day, day_key);
        if (bucket == NULL) {
            bucket = g_array_new(FALSE, FALSE, sizeof(RecordedSample));
            g_hash_table_insert(by_day, g_strdup(day_key), bucket);
        }

        RecordedSample rs = {0};
        rs.minute_of_day = mod;
        rs.steps = r->steps;
        rs.active_calories = r->active_calories;
        rs.distance_cm = r->distance_cm;
        rs.heart_rate = r->heart_rate;
        rs.energy = r->energy;
        rs.spo2 = r->spo2;
        rs.stress = r->stress;
        rs.sleep_stage = -1;
        rs.sleep_source = 0;
        rs.intensity = r->raw_intensity > 0 && r->raw_intensity <= 7 ? r->raw_intensity : 0;
        g_array_append_val(bucket, rs);
    }
    salus_local_day_cursor_clear(&cursor);
    flush_recorded_day_buckets(state, by_day);
    g_hash_table_unref(by_day);
}

static GHashTable *daily_summary_table_new(void) {
    return g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
}

/* Stores the band's summary for its local day. Main thread only (the dashboard reads it there). */
void health_apply_daily_summary(SalusApp *state, const SalusDailySummaryRow *row) {
    if (state == NULL || row == NULL || row->timestamp_ms <= 0) {
        return;
    }
    gchar *day_key = salus_local_day_key(row->timestamp_ms / 1000LL);
    if (day_key == NULL) {
        return;
    }
    g_mutex_lock(&state->recorded_lock);
    if (state->daily_summaries == NULL) {
        state->daily_summaries = daily_summary_table_new();
    }
    g_hash_table_replace(state->daily_summaries, day_key, g_memdup2(row, sizeof(*row)));
    g_mutex_unlock(&state->recorded_lock);
    salus_dashboard_schedule_refresh(state);
}

typedef struct {
    gint64 bed_sec;
    gint64 wake_sec;
} RebuildSleepSession;

typedef struct {
    SalusApp *state;
    GHashTable *days;
    GHashTable *summaries;
    GArray *sleep_stage_markers;
    GArray *sleep_sessions; /* RebuildSleepSession, ordered by bed time */
    GHashTable *sleep_summaries;
    SalusLocalDayCursor cursor;
} RecordedRebuildCtx;

/* A stage marker outside any recorded bed→wake session lasts until the next marker, but no
 * longer than this (largest in-session gap observed on Band 9/10 data is ~2.5 h). */
#define REBUILD_ORPHAN_MARKER_MAX_SEC (3 * 3600)

static void sleep_duration_value_free(gpointer data) {
    g_free(data);
}

static void rebuild_store_sleep_durations(RecordedRebuildCtx *ctx, const SalusSleepTimeRow *r) {
    if (ctx == NULL || r == NULL || ctx->sleep_summaries == NULL) {
        return;
    }
    SalusSleepStageDurations nd = {
        .deep_min = r->deep_min,
        .light_min = r->light_min,
        .rem_min = r->rem_min,
        .awake_min = r->awake_min,
        .total_min = r->total_min,
    };
    salus_sleep_normalize_stage_durations(&nd);
    const int deep = nd.deep_min > 0 ? nd.deep_min : 0;
    const int light = nd.light_min > 0 ? nd.light_min : 0;
    const int rem = nd.rem_min > 0 ? nd.rem_min : 0;
    const int awake = nd.awake_min > 0 ? nd.awake_min : 0;
    const int sum = deep + light + rem + awake;
    if (sum <= 0 && r->total_min <= 0) {
        return;
    }

    gchar *wake_day = salus_local_day_key(r->wake_ms / 1000LL);
    if (wake_day == NULL) {
        return;
    }
    SalusSleepDurationSummary *sum_row = g_hash_table_lookup(ctx->sleep_summaries, wake_day);
    if (sum_row == NULL) {
        sum_row = g_new0(SalusSleepDurationSummary, 1);
        g_hash_table_insert(ctx->sleep_summaries, wake_day, sum_row);
    } else {
        g_free(wake_day);
    }
    sum_row->from_db = TRUE;
    if (r->wake_ms > r->bed_ms && r->bed_ms > 0) {
        const gint64 bed = r->bed_ms / 1000LL, wake = r->wake_ms / 1000LL;
        sum_row->bed_sec = sum_row->bed_sec == 0 ? bed : MIN(sum_row->bed_sec, bed);
        sum_row->wake_sec = MAX(sum_row->wake_sec, wake);
    }
    if (sum > 0) {
        sum_row->deep = deep;
        sum_row->light = light;
        sum_row->rem = rem;
        sum_row->awake = awake;
        const int asleep = deep + light + rem;
        if (asleep > 0) {
            sum_row->total_asleep = asleep;
        }
    }
    if (r->total_min > 0) {
        if (sum_row->total_asleep <= 0) {
            sum_row->total_asleep = r->total_min;
        }
        if (r->wake_ms > r->bed_ms) {
            const int span = (int)((r->wake_ms - r->bed_ms + 59999LL) / 60000LL);
            if (r->total_min == span && sum > 0) {
                const int asleep = deep + light + rem;
                if (asleep > 0 && asleep < span) {
                    sum_row->total_asleep = asleep;
                }
            } else if (sum_row->total_asleep <= 0 || r->total_min < sum_row->total_asleep) {
                sum_row->total_asleep = r->total_min;
            }
        }
    }
}

/* Minute slot for a local timestamp, creating the day array on first use. */
static RecordedSample *rebuild_minute_slot(RecordedRebuildCtx *ctx, gint64 unix_sec) {
    const gchar *day_key = NULL;
    guint16 mod = 0;
    if (!salus_local_day_cursor_resolve(&ctx->cursor, unix_sec, &day_key, &mod)) {
        return NULL;
    }
    GArray *day = g_hash_table_lookup(ctx->days, day_key);
    if (day == NULL) {
        day = recorded_create_day_array();
        g_hash_table_insert(ctx->days, g_strdup(day_key), day);
    }
    if (mod >= day->len) {
        return NULL;
    }
    return &g_array_index(day, RecordedSample, mod);
}

/* Sets a minute's sleep stage; a negative stage ("not asleep") clears it. */
static void rebuild_set_sleep_minute(RecordedRebuildCtx *ctx, gint64 unix_sec, int stage) {
    RecordedSample *dst = rebuild_minute_slot(ctx, unix_sec);
    if (dst == NULL) {
        return;
    }
    dst->sleep_stage = stage >= 0 ? stage : -1;
    dst->sleep_source = stage >= 0 ? 1 : 0;
}

/* TRUE when a stage marker falls inside [bed, wake). Markers are sorted by time. */
static gboolean rebuild_session_has_markers(const RecordedRebuildCtx *ctx, gint64 bed_sec, gint64 wake_sec) {
    const GArray *m = ctx->sleep_stage_markers;
    for (guint i = 0; m != NULL && i < m->len; i++) {
        const gint64 t = g_array_index(m, SalusSleepStageRow, i).timestamp_ms / 1000LL;
        if (t >= wake_sec) {
            break;
        }
        if (t >= bed_sec) {
            return TRUE;
        }
    }
    return FALSE;
}

static const RebuildSleepSession *rebuild_find_sleep_session(const RecordedRebuildCtx *ctx, gint64 unix_sec) {
    if (ctx->sleep_sessions == NULL) {
        return NULL;
    }
    for (guint i = 0; i < ctx->sleep_sessions->len; i++) {
        const RebuildSleepSession *s = &g_array_index(ctx->sleep_sessions, RebuildSleepSession, i);
        if (unix_sec >= s->bed_sec && unix_sec < s->wake_sec) {
            return s;
        }
    }
    return NULL;
}

static void rebuild_expand_sleep_stage_markers(RecordedRebuildCtx *ctx) {
    if (ctx == NULL || ctx->sleep_stage_markers == NULL || ctx->sleep_stage_markers->len == 0) {
        return;
    }
    GArray *m = ctx->sleep_stage_markers;
    for (guint i = 0; i < m->len; i++) {
        const SalusSleepStageRow *p = &g_array_index(m, SalusSleepStageRow, i);
        const gint64 start_sec = p->timestamp_ms / 1000LL;
        gint64 end_sec;
        if (i + 1 < m->len) {
            end_sec = g_array_index(m, SalusSleepStageRow, i + 1).timestamp_ms / 1000LL;
        } else {
            end_sec = start_sec + 60;
        }
        /* A stage lasts until the next marker, but never past the end of its own night:
         * without this the last marker of a night was painted until the next night's first one. */
        const RebuildSleepSession *session = rebuild_find_sleep_session(ctx, start_sec);
        if (session != NULL) {
            end_sec = MIN(end_sec, session->wake_sec);
        } else {
            end_sec = MIN(end_sec, start_sec + REBUILD_ORPHAN_MARKER_MAX_SEC);
        }
        if (end_sec <= start_sec) {
            end_sec = start_sec + 60;
        }
        const int stage = salus_normalize_sleep_stage(p->stage);
        for (gint64 sec = start_sec; sec < end_sec; sec += 60) {
            rebuild_set_sleep_minute(ctx, sec, stage);
        }
    }
}

static gboolean rebuild_activity_cb(const SalusXiaomiActivityRow *r, gpointer user_data) {
    RecordedRebuildCtx *ctx = user_data;
    RecordedSample *dst = rebuild_minute_slot(ctx, (gint64)r->timestamp_sec);
    if (dst == NULL) {
        return TRUE;
    }
    if (r->steps >= 0) {
        dst->steps = r->steps;
    }
    if (r->active_calories >= 0) {
        dst->active_calories = r->active_calories;
    }
    if (r->distance_cm >= 0) {
        dst->distance_cm = r->distance_cm;
    }
    if (r->heart_rate >= 0) {
        dst->heart_rate = r->heart_rate;
    }
    if (r->energy >= 0) {
        dst->energy = r->energy;
    }
    if (r->spo2 >= 0) {
        dst->spo2 = r->spo2;
    }
    if (r->stress >= 0) {
        dst->stress = r->stress;
    }
    if (r->raw_intensity > 0 && r->raw_intensity <= 7) {
        dst->intensity = r->raw_intensity;
    }
    return TRUE;
}

static gboolean rebuild_daily_summary_cb(const SalusDailySummaryRow *r, gpointer user_data) {
    RecordedRebuildCtx *ctx = user_data;
    const gchar *day_key = NULL;
    guint16 mod = 0;
    if (r->timestamp_ms > 0 &&
        salus_local_day_cursor_resolve(&ctx->cursor, r->timestamp_ms / 1000LL, &day_key, &mod)) {
        g_hash_table_replace(ctx->summaries, g_strdup(day_key), g_memdup2(r, sizeof(*r)));
    }
    return TRUE;
}

static gboolean rebuild_sleep_stage_cb(const SalusSleepStageRow *r, gpointer user_data) {
    RecordedRebuildCtx *ctx = user_data;
    if (ctx->sleep_stage_markers == NULL) {
        ctx->sleep_stage_markers = g_array_new(FALSE, FALSE, sizeof(SalusSleepStageRow));
    }
    SalusSleepStageRow copy = *r;
    g_array_append_val(ctx->sleep_stage_markers, copy);
    return TRUE;
}

static gboolean rebuild_sleep_time_cb(const SalusSleepTimeRow *r, gpointer user_data) {
    RecordedRebuildCtx *ctx = user_data;
    rebuild_store_sleep_durations(ctx, r);
    const gint64 bed_sec = r->bed_ms / 1000LL;
    const gint64 wake_sec = r->wake_ms / 1000LL;
    if (wake_sec <= bed_sec) {
        return TRUE;
    }
    const RebuildSleepSession session = {.bed_sec = bed_sec, .wake_sec = wake_sec};
    g_array_append_val(ctx->sleep_sessions, session);
    /* Without a stage timeline the night is "asleep, stage unknown"; the band's stage totals (if
     * any) still give the breakdown. Never invent a timeline from the totals. */
    if (!rebuild_session_has_markers(ctx, bed_sec, wake_sec)) {
        for (gint64 sec = bed_sec; sec < wake_sec; sec += 60) {
            rebuild_set_sleep_minute(ctx, sec, SALUS_SLEEP_STAGE_UNKNOWN);
        }
    }
    return TRUE;
}

/* Rebuilds the per-minute day timelines from SQLite. Main thread only. */
void recorded_rebuild_from_db(SalusApp *state) {
    if (state == NULL || state->selected_address == NULL || state->selected_address[0] == '\0') {
        return;
    }
    if (!salus_db_ensure_device(state->selected_address)) {
        return;
    }

    RecordedRebuildCtx ctx = {0};
    ctx.state = state;
    ctx.days = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, recorded_day_value_free);
    ctx.summaries = daily_summary_table_new();
    ctx.sleep_summaries = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, sleep_duration_value_free);
    ctx.sleep_sessions = g_array_new(FALSE, FALSE, sizeof(RebuildSleepSession));
    salus_local_day_cursor_init(&ctx.cursor);

    (void)salus_db_foreach_xiaomi_activity(rebuild_activity_cb, &ctx);
    (void)salus_db_foreach_sleep_stage(rebuild_sleep_stage_cb, &ctx); /* before the sessions */
    (void)salus_db_foreach_sleep_time(rebuild_sleep_time_cb, &ctx);
    (void)salus_db_foreach_daily_summary(rebuild_daily_summary_cb, &ctx);
    rebuild_expand_sleep_stage_markers(&ctx);
    if (ctx.sleep_stage_markers != NULL) {
        g_array_unref(ctx.sleep_stage_markers);
    }
    g_array_unref(ctx.sleep_sessions);
    salus_local_day_cursor_clear(&ctx.cursor);

    g_mutex_lock(&state->recorded_lock);
    if (state->recorded_days != NULL) {
        g_hash_table_unref(state->recorded_days);
    }
    state->recorded_days = ctx.days;

    if (state->daily_summaries != NULL) {
        g_hash_table_unref(state->daily_summaries);
    }
    state->daily_summaries = ctx.summaries;

    if (state->sleep_duration_by_day != NULL) {
        g_hash_table_unref(state->sleep_duration_by_day);
    }
    state->sleep_duration_by_day = ctx.sleep_summaries;
    g_mutex_unlock(&state->recorded_lock);

    recorded_refresh_ui(state);
}

static gboolean recorded_rebuild_idle_cb(gpointer user_data) {
    SalusApp *state = user_data;
    state->recorded_rebuild_source_id = 0;
    recorded_rebuild_from_db(state);
    salus_dashboard_schedule_refresh(state);
    return G_SOURCE_REMOVE;
}

void recorded_schedule_rebuild(SalusApp *state) {
    if (state == NULL || state->recorded_rebuild_source_id != 0) {
        return; /* a pending rebuild will read everything written to the DB before it runs */
    }
    state->recorded_rebuild_source_id = g_idle_add(recorded_rebuild_idle_cb, state);
}

void salus_ui_reload_data(SalusApp *state) {
    recorded_rebuild_from_db(state);
    salus_sport_load_from_db(state);
    salus_dashboard_schedule_refresh(state);
}

static void show_main(SalusApp *state) {
    salus_ui_reload_data(state);
    if (state->stack != NULL) {
        gtk_stack_set_visible_child_name(GTK_STACK(state->stack), "main");
    }
    salus_dashboard_update_device_state(state, NULL, NULL, NULL);
    salus_dashboard_schedule_refresh(state);
}

/* The device connected. */
void switch_to_dashboard(SalusApp *state) {
    show_main(state);
    gchar *msg = g_strdup_printf("Connected to %s",
                                 state->device_display_name != NULL ? state->device_display_name : "the band");
    salus_dashboard_toast(state, msg);
    g_free(msg);
}

/* Connection lost: saved data stays browsable; only fall back to setup without a saved band. */
void switch_to_pairing(SalusApp *state) {
    if (salus_ui_has_saved_device(state)) {
        salus_dashboard_toast(state, "Connection to the band was lost");
        salus_dashboard_schedule_refresh(state);
    } else {
        salus_ui_show_setup(state);
    }
}

void salus_app_shutdown(GApplication *app, gpointer user_data) {
    (void)app;
    SalusApp *state = user_data;
    salus_find_phone_shutdown(state);
    salus_weather_shutdown(state);
    salus_app_set_device(state, NULL); /* disconnects and frees the driver session */
    g_clear_pointer(&state->saved_devices, g_ptr_array_unref);
    if (state->log_flush_id != 0) {
        g_source_remove(state->log_flush_id);
        state->log_flush_id = 0;
    }
    g_mutex_lock(&state->log_lock);
    if (state->log_queue != NULL) {
        while (g_queue_get_length(state->log_queue) > 0) {
            g_free(g_queue_pop_head(state->log_queue));
        }
        g_queue_free(state->log_queue);
        state->log_queue = NULL;
    }
    g_mutex_unlock(&state->log_lock);
    if (state->recorded_rebuild_source_id != 0) {
        g_source_remove(state->recorded_rebuild_source_id);
        state->recorded_rebuild_source_id = 0;
    }
    g_mutex_lock(&state->recorded_lock);
    if (state->recorded_days != NULL) {
        g_hash_table_unref(state->recorded_days);
        state->recorded_days = NULL;
    }
    if (state->daily_summaries != NULL) {
        g_hash_table_unref(state->daily_summaries);
        state->daily_summaries = NULL;
    }
    if (state->sleep_duration_by_day != NULL) {
        g_hash_table_unref(state->sleep_duration_by_day);
        state->sleep_duration_by_day = NULL;
    }
    g_mutex_unlock(&state->recorded_lock);
    g_free(state->weather_location_name);
    g_free(state->weather_last_status);
    g_free(state->notification_status);
    salus_notifications_shutdown(state);
    state->weather_location_name = NULL;
    state->weather_last_status = NULL;
    if (state->health_view_date != NULL) {
        g_date_free(state->health_view_date);
        state->health_view_date = NULL;
    }
    salus_sport_entries_clear(state);
    if (state->dash_ui != NULL) {
        salus_dashboard_ui_free(state->dash_ui);
        state->dash_ui = NULL;
    }
    salus_connect_free(state);
    salus_db_close();
}

void salus_activate(GApplication *application, gpointer user_data) {
    SalusApp *state = user_data;
    if (state->window != NULL) {
        gtk_window_present(GTK_WINDOW(state->window));
        return;
    }
    if (!salus_db_open()) {
        g_warning("Gadgetbridge-compatible database could not be opened; samples will not be persisted.");
    }
    ui_style_install(gdk_display_get_default());
    state->log_buffer = gtk_text_buffer_new(NULL);

    state->window = adw_application_window_new(GTK_APPLICATION(application));
    gtk_window_set_title(GTK_WINDOW(state->window), "Salus");
    gtk_window_set_default_size(GTK_WINDOW(state->window), 980, 820);
    gtk_widget_set_size_request(state->window, 360, 480);


    state->dash_ui = salus_dashboard_ui_new();
    state->page_dashboard = salus_dashboard_build(state, state->dash_ui);
    state->page_connect = salus_connect_build(state);

    state->stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(state->stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_add_named(GTK_STACK(state->stack), state->page_dashboard, "main");
    gtk_stack_add_named(GTK_STACK(state->stack), state->page_connect, "setup");

    state->toast_overlay = adw_toast_overlay_new();
    adw_toast_overlay_set_child(ADW_TOAST_OVERLAY(state->toast_overlay), state->stack);
    salus_dashboard_set_toast_overlay(state->dash_ui, state->toast_overlay);
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(state->window), state->toast_overlay);
    salus_dashboard_add_breakpoints(state->window, state->dash_ui);

    if (salus_ui_has_saved_device(state)) {
        show_main(state);
    } else {
        salus_ui_show_setup(state);
    }
    gtk_window_present(GTK_WINDOW(state->window));

    if (state->notifications_enabled) {
        salus_notifications_start(state);
    }
    salus_dashboard_schedule_refresh(state);
}
