#include "miwear/app.h"
#include "miwear/activity/daily_details.h"
#include "miwear/ui/dashboard.h"
#include "miwear/ui/health_data.h"
#include "miwear/activity/mi_fitness_full_day_sleep.h"
#include "miwear/activity/manual.h"
#include "miwear/sleep/mi_sleep_packets.h"
#include "miwear/db/db.h"
#include "miwear/spp/wire.h"
#include "miwear/activity/mi_fitness_dispatch.h"
#include "miwear/activity/workout_summary.h"
#include <gio/gio.h>
#include <math.h>
#include <stdio.h>

typedef struct {
    int total_min;
    int deep_min;
    int light_min;
    int rem_min;
    int awake_min;
    gboolean valid;
} SleepSummaryMinutes;

static gboolean sleep_header_valid_bit(const uint8_t *header, size_t header_len, int idx);


#include "xiaomi.pb-c.h"
static uint32_t crc32_ieee(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 1u) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

static guint16 local_minute_of_day(gint64 unix_sec) {
    GDateTime *dt = g_date_time_new_from_unix_local(unix_sec);
    if (dt == NULL) {
        return 0;
    }
    const guint16 mod = (guint16)(g_date_time_get_hour(dt) * 60 + g_date_time_get_minute(dt));
    g_date_time_unref(dt);
    return mod;
}

static gchar *fmt_unix_ts(uint32_t ts) {
    GDateTime *dt = g_date_time_new_from_unix_local((gint64)ts);
    if (dt == NULL) {
        return g_strdup_printf("%u", ts);
    }
    gchar *out = g_date_time_format(dt, "%Y-%m-%d %H:%M:%S");
    g_date_time_unref(dt);
    return out;
}

static void log_sleep_samples_summary(AppState *state, const char *tag, const GArray *samples) {
    if (samples == NULL || samples->len == 0) {
        gchar *line = g_strdup_printf("%s: no samples", tag);
        append_log(state, line);
        g_free(line);
        return;
    }

    gboolean seen = FALSE;
    guint first_m = 0, last_m = 0;
    guint c_deep = 0, c_light = 0, c_rem = 0, c_awake = 0, c_other = 0;
    for (guint i = 0; i < samples->len; i++) {
        const RecordedSample *s = &g_array_index(samples, RecordedSample, i);
        if (!seen || s->minute_of_day < first_m) first_m = s->minute_of_day;
        if (!seen || s->minute_of_day > last_m) last_m = s->minute_of_day;
        seen = TRUE;
        switch (s->sleep_stage) {
            case 2: c_deep++; break;
            case 3: c_light++; break;
            case 4: c_rem++; break;
            case 5: c_awake++; break;
            default: c_other++; break;
        }
    }

    gchar *line = g_strdup_printf(
        "%s: first=%02u:%02u last=%02u:%02u stages{deep=%u light=%u rem=%u awake=%u other=%u} samples=%u",
        tag,
        first_m / 60u, first_m % 60u,
        last_m / 60u, last_m % 60u,
        c_deep, c_light, c_rem, c_awake, c_other,
        samples->len
    );
    append_log(state, line);
    g_free(line);
}

typedef struct {
    gint64 from, to;
    guint minutes, steps, kcal, kcal_minutes;
    guint intensity[9]; /* 0..7, [8] = not stored (older build or not valid) */
    guint kcal_by_intensity[9];
    guint type[5];      /* 0..3, [4] = not stored */
} DayActivityTotals;

static gboolean day_totals_row(const MiwearXiaomiActivityRow *r, gpointer user_data) {
    DayActivityTotals *t = user_data;
    if (r->timestamp_sec < t->from || r->timestamp_sec >= t->to) {
        return TRUE;
    }
    t->minutes++;
    if (r->steps > 0) {
        t->steps += (guint)r->steps;
    }
    const int lvl = (r->raw_intensity >= 0 && r->raw_intensity <= 7) ? r->raw_intensity : 8;
    t->intensity[lvl]++;
    t->type[(r->raw_kind >= 0 && r->raw_kind <= 3) ? r->raw_kind : 4]++;
    if (r->active_calories > 0) {
        t->kcal += (guint)r->active_calories;
        t->kcal_minutes++;
        t->kcal_by_intensity[lvl] += (guint)r->active_calories;
    }
    return TRUE;
}

/* Whole-day sums over every stored minute of the local day that contains @any_sec. The band only
 * sends minutes since the last acked file, so one file's sums never cover the day. */
static void log_day_activity_totals(AppState *state, gint64 any_sec) {
    GDateTime *t = g_date_time_new_from_unix_local(any_sec);
    if (t == NULL) {
        return;
    }
    GDateTime *day = g_date_time_new_local(g_date_time_get_year(t), g_date_time_get_month(t),
                                           g_date_time_get_day_of_month(t), 0, 0, 0);
    GDateTime *next = g_date_time_add_days(day, 1);
    DayActivityTotals tot = {.from = g_date_time_to_unix(day), .to = g_date_time_to_unix(next)};
    gchar *day_txt = g_date_time_format(day, "%Y-%m-%d");
    g_date_time_unref(next);
    g_date_time_unref(day);
    g_date_time_unref(t);
    if (miwear_db_foreach_xiaomi_activity(day_totals_row, &tot)) {
        gchar *line = g_strdup_printf(
            "Day activity totals %s: minutes=%u steps_sum=%u kcal_sum=%u kcal_minutes=%u "
            "intensity{0=%u 1=%u 2=%u 3=%u 4=%u 5=%u 6=%u 7=%u n/a=%u} "
            "kcal_by_intensity{0=%u 1=%u 2=%u 3=%u 4=%u 5=%u 6=%u 7=%u n/a=%u} "
            "type{unknown=%u still=%u walk=%u run=%u n/a=%u}",
            day_txt, tot.minutes, tot.steps, tot.kcal, tot.kcal_minutes,
            tot.intensity[0], tot.intensity[1], tot.intensity[2], tot.intensity[3], tot.intensity[4],
            tot.intensity[5], tot.intensity[6], tot.intensity[7], tot.intensity[8],
            tot.kcal_by_intensity[0], tot.kcal_by_intensity[1], tot.kcal_by_intensity[2],
            tot.kcal_by_intensity[3], tot.kcal_by_intensity[4], tot.kcal_by_intensity[5],
            tot.kcal_by_intensity[6], tot.kcal_by_intensity[7], tot.kcal_by_intensity[8],
            tot.type[0], tot.type[1], tot.type[2], tot.type[3], tot.type[4]);
        append_log(state, line);
        g_free(line);
    }
    g_free(day_txt);
}

static void parse_daily_details_for_log(AppState *state,
                                        uint32_t ts,
                                        int version,
                                        const uint8_t *payload,
                                        size_t payload_len) {
    const char *error = NULL;
    GArray *minutes_arr = miwear_daily_details_decode(version, payload, payload_len, &error);
    if (minutes_arr == NULL) {
        gchar *msg = g_strdup_printf("Daily details parser (v%d): %s.", version, error != NULL ? error : "failed");
        append_log(state, msg);
        g_free(msg);
        return;
    }

    uint32_t minutes = 0;
    uint32_t hr_count = 0;
    uint32_t hr_sum = 0;
    uint32_t hr_min = 255;
    uint32_t hr_max = 0;
    uint32_t step_points = 0;
    uint32_t distance_points = 0;
    uint32_t kcal_sum = 0, steps_sum = 0, kcal_minutes = 0;
    uint32_t intensity_n[9] = {0}; /* 0..7, [8] = not valid */
    uint32_t type_n[5] = {0};      /* 0..3, [4] = not valid */
    uint32_t sport_minutes = 0;
    uint32_t kcal_by_intensity[9] = {0};
    GString *hr_track = g_string_new("");
    GArray *samples = g_array_new(FALSE, FALSE, sizeof(RecordedSample));
    /* Per sample: intensity << 8 | activity type, each 0xff when not valid. Stored as the
     * row's rawIntensity / rawKind. */
    GArray *raw_levels = g_array_new(FALSE, FALSE, sizeof(int));
    for (guint i = 0; i < minutes_arr->len; i++) {
        const MiwearDailyDetailsMinute *m = &g_array_index(minutes_arr, MiwearDailyDetailsMinute, i);
        minutes++;
        RecordedSample sample = {0};
        sample.steps = m->steps;
        sample.active_calories = m->active_calories;
        sample.distance_cm = m->distance_cm;
        sample.heart_rate = -1;
        sample.energy = m->energy;
        sample.spo2 = m->spo2;
        sample.stress = m->stress;
        sample.sleep_stage = -1;
        if (sample.steps > 0) {
            step_points++;
            steps_sum += (uint32_t)sample.steps;
        }
        const int lvl = (m->intensity >= 0 && m->intensity <= 7) ? m->intensity : 8;
        intensity_n[lvl]++;
        type_n[(m->active_type >= 0 && m->active_type <= 3) ? m->active_type : 4]++;
        if (m->sport_type > 0) {
            sport_minutes++;
        }
        if (m->active_calories > 0) {
            kcal_sum += (uint32_t)m->active_calories;
            kcal_minutes++;
            kcal_by_intensity[lvl] += (uint32_t)m->active_calories;
        }
        if (sample.distance_cm > 0) {
            distance_points++;
        }
        if (m->heart_rate > 0 && m->heart_rate <= 220) {
            const uint32_t hr = (uint32_t)m->heart_rate;
            sample.heart_rate = m->heart_rate;
            hr_count++;
            hr_sum += hr;
            hr_min = MIN(hr_min, hr);
            hr_max = MAX(hr_max, hr);
            if (hr_track->len < 350) {
                g_string_append_printf(hr_track, "%s%u", hr_track->len ? "," : "", hr);
            }
        }
        g_array_append_val(samples, sample);
        const int packed = ((m->intensity >= 0 ? m->intensity : 0xff) << 8) | (m->active_type >= 0 ? m->active_type : 0xff);
        g_array_append_val(raw_levels, packed);
    }
    g_array_unref(minutes_arr);

    gchar *ts_txt = fmt_unix_ts(ts);
    gchar *line = g_strdup_printf("Daily detail %s: minutes=%u, HR samples=%u, HR min/avg/max=%u/%u/%u",
                                  ts_txt,
                                  minutes,
                                  hr_count,
                                  hr_count ? hr_min : 0,
                                  hr_count ? (hr_sum / hr_count) : 0,
                                  hr_count ? hr_max : 0);
    append_log(state, line);
    g_free(line);

    /* For matching the band's own calories / active minutes against the per-minute data. */
    line = g_strdup_printf(
        "Daily detail %s activity: steps_sum=%u kcal_sum=%u kcal_minutes=%u "
        "intensity{0=%u 1=%u 2=%u 3=%u 4=%u 5=%u 6=%u 7=%u n/a=%u} "
        "kcal_by_intensity{0=%u 1=%u 2=%u 3=%u 4=%u 5=%u 6=%u 7=%u n/a=%u} "
        "type{unknown=%u still=%u walk=%u run=%u n/a=%u} sport_minutes=%u",
        ts_txt, steps_sum, kcal_sum, kcal_minutes,
        intensity_n[0], intensity_n[1], intensity_n[2], intensity_n[3], intensity_n[4],
        intensity_n[5], intensity_n[6], intensity_n[7], intensity_n[8],
        kcal_by_intensity[0], kcal_by_intensity[1], kcal_by_intensity[2], kcal_by_intensity[3],
        kcal_by_intensity[4], kcal_by_intensity[5], kcal_by_intensity[6], kcal_by_intensity[7],
        kcal_by_intensity[8],
        type_n[0], type_n[1], type_n[2], type_n[3], type_n[4], sport_minutes);
    append_log(state, line);
    g_free(line);

    if (hr_count > 0) {
        gchar *track = g_strdup_printf("HR track: %s%s",
                                       hr_track->str,
                                       (hr_track->len >= 350) ? ",..." : "");
        append_log(state, track);
        g_free(track);
    }

    if (samples->len > 0) {
        /* Matches DailyDetailsParser.java: first payload row = file-id timestamp (local minute),
         * each following row +1 minute. Split across calendar days when a file spans midnight. */
        GDateTime *wall = g_date_time_new_from_unix_local((gint64)ts);
        if (wall != NULL) {
            GDateTime *start_min = g_date_time_new(
                g_date_time_get_timezone(wall),
                g_date_time_get_year(wall),
                g_date_time_get_month(wall),
                g_date_time_get_day_of_month(wall),
                g_date_time_get_hour(wall),
                g_date_time_get_minute(wall),
                0.0);
            g_date_time_unref(wall);
            if (start_min != NULL) {
                GArray *act_rows = g_array_sized_new(FALSE, FALSE, sizeof(MiwearXiaomiActivityRow), samples->len);
                const gint64 now_sec = g_get_real_time() / G_USEC_PER_SEC;
                guint future_rows = 0;
                for (guint i = 0; i < samples->len; i++) {
                    GDateTime *row_ts = g_date_time_add_minutes(start_min, (gint)i);
                    gint64 unix_sec = g_date_time_to_unix(row_ts);
                    g_date_time_unref(row_ts);
                    if (unix_sec > now_sec + 60) {
                        /* A file can't describe minutes that haven't happened: a mis-decoded file
                         * shows up as rows past "now". Never store those. */
                        future_rows = samples->len - i;
                        break;
                    }
                    const RecordedSample *src = &g_array_index(samples, RecordedSample, i);
                    /* Mirror DailyDetailsParser + XiaomiActivitySample defaults (ActivitySample.NOT_MEASURED = -1). */
                    MiwearXiaomiActivityRow r = {0};
                    r.timestamp_sec = (int32_t)unix_sec;
                    const int packed = g_array_index(raw_levels, int, i);
                    r.raw_intensity = (packed >> 8) != 0xff ? (packed >> 8) : -1;
                    r.raw_kind = (packed & 0xff) != 0xff ? (packed & 0xff) : -1;
                    r.heart_rate = src->heart_rate;
                    r.stress = src->stress;
                    r.spo2 = src->spo2;
                    r.distance_cm = src->distance_cm >= 0 ? src->distance_cm : -1;
                    r.active_calories = src->active_calories >= 0 ? src->active_calories : -1;
                    r.energy = src->energy >= 0 ? src->energy : -1;
                    r.steps = src->steps >= 0 ? src->steps : -1;
                    g_array_append_val(act_rows, r);
                }
                if (future_rows > 0) {
                    gchar *w = g_strdup_printf("Daily details: dropped %u row(s) dated in the future "
                                               "(file decoded with the wrong layout?).", future_rows);
                    append_log(state, w);
                    g_free(w);
                }
                recorded_merge_xiaomi_activity_rows(state,
                                                    (const MiwearXiaomiActivityRow *)act_rows->data,
                                                    act_rows->len);
                (void)miwear_db_replace_xiaomi_activity_batch((const MiwearXiaomiActivityRow *)act_rows->data, act_rows->len);
                int latest_stress = -1;
                int32_t latest_stress_ts = 0;
                for (guint ri = 0; ri < act_rows->len; ri++) {
                    const MiwearXiaomiActivityRow *row = &g_array_index(act_rows, MiwearXiaomiActivityRow, ri);
                    if (miwear_stress_value_valid(row->stress) && row->timestamp_sec >= latest_stress_ts) {
                        latest_stress_ts = row->timestamp_sec;
                        latest_stress = row->stress;
                    }
                }
                if (latest_stress >= 0) {
                    state->live_stress_ts_sec = latest_stress_ts;
                    gchar *stress_txt = g_strdup_printf("%d", latest_stress);
                    schedule_metrics_update(state,
                                            FALSE,
                                            0,
                                            FALSE,
                                            0,
                                            FALSE,
                                            0,
                                            FALSE,
                                            0,
                                            FALSE,
                                            0,
                                            FALSE,
                                            0,
                                            NULL,
                                            stress_txt);
                    g_free(stress_txt);
                    miwear_dashboard_refresh(state);
                }
                if (act_rows->len > 0) {
                    log_day_activity_totals(state,
                                            g_array_index(act_rows, MiwearXiaomiActivityRow, act_rows->len - 1).timestamp_sec);
                }
                g_array_unref(act_rows);
                g_date_time_unref(start_min);
            }
        }
    }

    if (step_points == 0 && distance_points == 0) {
        append_log(state,
                   "Daily details decoded but contained no minute step/distance points. "
                   "This usually means the band sent only daily summary data or the details format changed.");
    }

    g_free(ts_txt);
    g_string_free(hr_track, TRUE);
    g_array_unref(samples);
    g_array_unref(raw_levels);
}

static void parse_daily_summary_for_log(AppState *state,
                                        uint32_t ts,
                                        int8_t tz,
                                        int version,
                                        const uint8_t *payload,
                                        size_t payload_len) {
    int header_size = 0;
    if (version == 3) header_size = 3;
    else if (version == 5) header_size = 4;
    else {
        append_log(state, "Daily summary parser: unsupported version.");
        return;
    }
    if (payload_len < (size_t)(8 + header_size + 30)) {
        append_log(state, "Daily summary parser: payload too short.");
        return;
    }

    size_t off = (size_t)(8 + header_size);
    const uint32_t steps = miwear_read_u32_le(payload + off); off += 4;
    /* Mi Fitness daily_activity_report: totalactiveCalories (u16), then current heart rate (u8). */
    const uint16_t active_calories = miwear_read_u16_le(payload + off); off += 2;
    const uint8_t hr_now = payload[off++];
    const uint8_t hr_rest = payload[off++];
    const uint8_t hr_max = payload[off++];
    const uint32_t hr_max_ts = miwear_read_u32_le(payload + off); off += 4;
    const uint8_t hr_min = payload[off++];
    const uint32_t hr_min_ts = miwear_read_u32_le(payload + off); off += 4;
    const uint8_t hr_avg = payload[off++];
    const uint8_t stress_avg = payload[off++];
    const uint8_t stress_max = payload[off++];
    const uint8_t stress_min = payload[off++];
    const uint32_t standing_bits = (uint32_t)payload[off] | ((uint32_t)payload[off + 1] << 8) | ((uint32_t)payload[off + 2] << 16);
    off += 3;
    const uint16_t calories = miwear_read_u16_le(payload + off); off += 2; /* totalCalories */
    const uint16_t recovery_time = miwear_read_u16_le(payload + off); off += 2; /* residualrecoverytime */
    off += 1; /* current SpO2 */
    const uint8_t spo2_max = payload[off++];
    const uint32_t spo2_max_ts = miwear_read_u32_le(payload + off); off += 4;
    const uint8_t spo2_min = payload[off++];
    const uint32_t spo2_min_ts = miwear_read_u32_le(payload + off); off += 4;
    const uint8_t spo2_avg = payload[off++];

    gboolean has_training = FALSE;
    int t_day = 0, t_week = 0, t_level = 0, v_light = 0, v_mod = 0, v_high = 0, v_cur = 0;
    if (version > 3 && off + 10 <= payload_len) {
        t_day = (int)miwear_read_u16_le(payload + off);
        off += 2;
        t_week = (int)miwear_read_u16_le(payload + off);
        off += 2;
        t_level = (int)payload[off++];
        v_light = (int)payload[off++];
        v_mod = (int)payload[off++];
        v_high = (int)payload[off++];
        v_cur = (int)miwear_read_u16_le(payload + off);
        off += 2;
        has_training = TRUE;
    }

    gchar *ts_txt = fmt_unix_ts(ts);
    gchar *hr_max_txt = fmt_unix_ts(hr_max_ts);
    gchar *hr_min_txt = fmt_unix_ts(hr_min_ts);
    const guint standing_metric = miwear_standing_display_value((int)standing_bits);
    gchar *line = g_strdup_printf(
        "Daily summary %s (v%d): steps=%u totalCalories=%u totalactiveCalories=%u hr_now=%u recovery=%u "
        "HR(rest/avg/min/max)=%u/%u/%u/%u "
        "stress avg/min/max=%u/%u/%u (day aggregate, not last manual reading) standing=%u",
        ts_txt,
        version,
        steps,
        calories,
        active_calories,
        hr_now,
        recovery_time,
        hr_rest,
        hr_avg,
        hr_min,
        hr_max,
        stress_avg,
        stress_min,
        stress_max,
        standing_metric);
    append_log(state, line);
    g_free(line);
    line = g_strdup_printf("  HR extrema timestamps: min@%s max@%s", hr_min_txt, hr_max_txt);
    append_log(state, line);
    g_free(line);
    g_free(hr_min_txt);
    g_free(hr_max_txt);
    g_free(ts_txt);

    MiwearDailySummaryRow dr = {0};
    dr.timestamp_ms = (int64_t)ts * 1000LL;
    dr.timezone = (int)tz;
    dr.steps = (int)steps;
    dr.has_active_calories = TRUE;
    dr.active_calories = (int)active_calories;
    dr.hr_resting = (int)hr_rest;
    dr.hr_max = (int)hr_max;
    dr.hr_max_ts_sec = (int32_t)hr_max_ts;
    dr.hr_min = (int)hr_min;
    dr.hr_min_ts_sec = (int32_t)hr_min_ts;
    dr.hr_avg = (int)hr_avg;
    dr.stress_avg = (int)stress_avg;
    dr.stress_max = (int)stress_max;
    dr.stress_min = (int)stress_min;
    dr.standing_bits = (int)standing_bits;
    dr.calories = (int)calories;
    dr.spo2_max = (int)spo2_max;
    dr.spo2_max_ts_sec = (int32_t)spo2_max_ts;
    dr.spo2_min = (int)spo2_min;
    dr.spo2_min_ts_sec = (int32_t)spo2_min_ts;
    dr.spo2_avg = (int)spo2_avg;
    dr.has_training_vitality = has_training;
    dr.training_load_day = t_day;
    dr.training_load_week = t_week;
    dr.training_load_level = t_level;
    dr.vitality_increase_light = v_light;
    dr.vitality_increase_moderate = v_mod;
    dr.vitality_increase_high = v_high;
    dr.vitality_current = v_cur;
    (void)miwear_db_replace_daily_summary(&dr);
    health_apply_daily_summary(state, &dr);
}

static gboolean sleep_header_valid_bit(const uint8_t *header, size_t header_len, int idx) {
    if (idx < 0) return FALSE;
    size_t b = (size_t)idx / 8u;
    if (b >= header_len) return FALSE;
    return (header[b] & (1u << (7 - (idx % 8)))) != 0;
}

static void parse_sleep_stages_v2_for_log(AppState *state,
                                          uint32_t ts,
                                          const uint8_t *payload,
                                          size_t payload_len) {
    if (payload_len < 8 + 7 + 2 + 4 + 4 + 3 + 2 + 2 + 2 + 2 + 1 + 4) {
        append_log(state, "Sleep stages parser: payload too short.");
        return;
    }
    size_t off = 8;
    off += 7;
    const uint16_t sleep_duration = miwear_read_u16_le(payload + off); off += 2;
    const uint32_t bed_time = miwear_read_u32_le(payload + off); off += 4;
    const uint32_t wake_time = miwear_read_u32_le(payload + off); off += 4;
    off += 3;
    const uint16_t deep = miwear_read_u16_le(payload + off); off += 2;
    const uint16_t light = miwear_read_u16_le(payload + off); off += 2;
    const uint16_t rem = miwear_read_u16_le(payload + off); off += 2;
    const uint16_t awake = miwear_read_u16_le(payload + off); off += 2;
    off += 1;

    GArray *raw_phases = g_array_new(FALSE, FALSE, sizeof(GbRawSleepPhaseChange));
    RecordedBatch *sleep_batch = recorded_batch_new();
    /* v2 phase values are already in stored-stage form (2 deep … 5 awake), as in Gadgetbridge. */
    uint32_t prev_t = 0;
    int prev_stage = -1;
    while (off + 5 <= payload_len - 4) {
        uint32_t t = miwear_read_u32_le(payload + off); off += 4;
        uint8_t stage = payload[off++];
        if (t == 0) continue;
        GbRawSleepPhaseChange ph = { t, stage };
        g_array_append_val(raw_phases, ph);
        if (prev_t != 0 && prev_stage >= 0 && t > prev_t) {
            for (uint32_t sec = prev_t; sec < t; sec += 60) {
                recorded_batch_add_sleep_minute(sleep_batch, (gint64)sec, miwear_normalize_sleep_stage(prev_stage));
            }
        }
        prev_t = t;
        prev_stage = miwear_normalize_sleep_stage((int)stage);
    }
    if (prev_t != 0 && prev_stage >= 0 && wake_time > prev_t) {
        for (uint32_t sec = prev_t; sec < wake_time; sec += 60) {
            recorded_batch_add_sleep_minute(sleep_batch, (gint64)sec, miwear_normalize_sleep_stage(prev_stage));
        }
    }

    if (bed_time != 0 && wake_time != 0 && sleep_duration != 0) {
        (void)miwear_db_replace_sleep_stages_v2(bed_time,
                                           wake_time,
                                           sleep_duration,
                                           deep,
                                           light,
                                           rem,
                                           awake,
                                           raw_phases->len > 0 ? (const GbRawSleepPhaseChange *)raw_phases->data : NULL,
                                           raw_phases->len);
    }
    g_array_unref(raw_phases);

    recorded_batch_commit(state, sleep_batch);
    append_log(state, "Sleep stages v2 parsed and merged into Sleep chart.");

    gchar *bed_txt = fmt_unix_ts(bed_time);
    gchar *wake_txt = fmt_unix_ts(wake_time);
    gchar *line = g_strdup_printf("Sleep stages: bed=%s wake=%s total=%u deep=%u light=%u rem=%u awake=%u",
                                  bed_txt, wake_txt, sleep_duration, deep, light, rem, awake);
    append_log(state, line);
    g_free(line);
    append_log(state, "Sleep stage timeline stored in dedicated Sleep chart.");
    g_free(bed_txt);
    g_free(wake_txt);
}

static size_t parse_sleep_hr_series(GArray *samples,
                                    const uint8_t *payload,
                                    size_t payload_len,
                                    size_t off,
                                    uint32_t default_first_sec,
                                    uint16_t unit_sec,
                                    uint16_t count) {
    if (count == 0 || count > 4096 || off >= payload_len - 4) {
        return off;
    }
    uint32_t first_sec = default_first_sec;
    if (off + 4 <= payload_len - 4) {
        first_sec = miwear_read_u32_le(payload + off);
        off += 4;
    }
    for (uint16_t i = 0; i < count && off < payload_len - 4; i++) {
        const uint8_t hr = payload[off++];
        if (hr == 0 || hr >= 240) {
            continue;
        }
        const uint32_t sec = first_sec + (uint32_t)unit_sec * (uint32_t)i;
        RecordedSample s = {0};
        s.minute_of_day = local_minute_of_day((gint64)sec);
        s.steps = s.active_calories = s.distance_cm = s.energy = s.spo2 = s.stress = s.sleep_stage = -1;
        s.heart_rate = (int)hr;
        s.sleep_source = 1;
        g_array_append_val(samples, s);
    }
    return off;
}

static size_t parse_sleep_spo2_series(GArray *samples,
                                        const uint8_t *payload,
                                        size_t payload_len,
                                        size_t off,
                                        uint32_t default_first_sec,
                                        uint16_t unit_sec,
                                        uint16_t count) {
    if (count == 0 || count > 4096 || off >= payload_len - 4) {
        return off;
    }
    uint32_t first_sec = default_first_sec;
    if (off + 4 <= payload_len - 4) {
        first_sec = miwear_read_u32_le(payload + off);
        off += 4;
    }
    for (uint16_t i = 0; i < count && off < payload_len - 4; i++) {
        const uint8_t spo2 = payload[off++];
        if (spo2 == 0 || spo2 > 100) {
            continue;
        }
        const uint32_t sec = first_sec + (uint32_t)unit_sec * (uint32_t)i;
        RecordedSample s = {0};
        s.minute_of_day = local_minute_of_day((gint64)sec);
        s.steps = s.active_calories = s.distance_cm = s.energy = s.heart_rate = s.stress = s.sleep_stage = -1;
        s.spo2 = (int)spo2;
        s.sleep_source = 1;
        g_array_append_val(samples, s);
    }
    return off;
}

typedef struct {
    GArray *samples;
    GArray *stage_rows;
    GArray *pulse_rows;
    SleepSummaryMinutes *summary_out;
    guint stage_packets;
    long long last_hb_ms;
    uint32_t bed_sec;
    uint32_t wake_sec;
} ParseSleepFeatureCtx;

static gboolean parse_sleep_feature_packet(const MiwearSleepPacketView *pkt, void *user_data) {
    ParseSleepFeatureCtx *ctx = user_data;
    if (ctx == NULL || pkt == NULL) {
        return TRUE;
    }
    const uint8_t type = pkt->type;
    if (type == 0x02 || type == 0x03 || type == 0x09 || type == 0x0c || type == 0x0d || type == 0x0e || type == 0x0f) {
        return TRUE;
    }
    const uint8_t *d = pkt->data;
    const uint16_t data_len = pkt->data_len;

    if (type == 1) {
        const long long tms = (long long)pkt->ts_raw;
        if (llabs(ctx->last_hb_ms - tms) > 30000LL) {
            ctx->last_hb_ms = tms;
            MiwearHeartPulseRow hp0 = {ctx->last_hb_ms};
            g_array_append_val(ctx->pulse_rows, hp0);
        }
        for (uint16_t i = 0; i < data_len; i++) {
            const int delta = d[i];
            if (delta <= 0) {
                continue;
            }
            ctx->last_hb_ms += (long long)delta * 10LL;
            MiwearHeartPulseRow hp = {ctx->last_hb_ms};
            g_array_append_val(ctx->pulse_rows, hp);
            const int rr_ms = delta * 10;
            const int hr = rr_ms > 0 ? (60000 / rr_ms) : -1;
            if (hr > 0 && hr < 240) {
                const uint32_t sec = (uint32_t)(ctx->last_hb_ms / 1000LL);
                RecordedSample s = {0};
                s.minute_of_day = local_minute_of_day((gint64)sec);
                s.steps = s.active_calories = s.distance_cm = s.energy = s.spo2 = s.stress = s.sleep_stage = -1;
                s.heart_rate = hr;
                s.sleep_source = 1;
                g_array_append_val(ctx->samples, s);
            }
        }
    } else if (type == 17) {
        ctx->stage_packets++;
        int64_t current_ms = miwear_sleep_packet_timestamp_ms(pkt->ts_raw);
        if (!miwear_sleep_timestamp_plausible(current_ms)) {
            /* Gadgetbridge anchors each stage packet at its own timestamp. Chaining an unreadable
             * one onto the end of the previous packet duplicated stage runs past wake-up. */
            return TRUE;
        }
        for (uint16_t i = 0; i + 1 < data_len; i += 2) {
            const uint16_t val = ((uint16_t)d[i] << 8) | d[i + 1];
            const int stage = miwear_sleep_decode_raw_stage((val >> 12) & 0x0F);
            const int paint = miwear_normalize_sleep_stage(stage); /* -1 for "not asleep" */
            int offset_min = (int)(val & 0x0FFF);
            if (offset_min < 0 || offset_min > 720) {
                continue;
            }
            if (offset_min == 0) {
                offset_min = 1;
            }
            MiwearSleepStageRow gr = {current_ms, stage};
            g_array_append_val(ctx->stage_rows, gr);
            for (int m = 0; paint >= 0 && m < offset_min; m++) {
                RecordedSample s = {0};
                s.minute_of_day = local_minute_of_day(current_ms / 1000LL + (gint64)m * 60LL);
                s.steps = s.active_calories = s.distance_cm = s.energy = s.heart_rate = s.spo2 = s.stress = -1;
                s.sleep_stage = paint;
                s.sleep_source = 1;
                g_array_append_val(ctx->samples, s);
            }
            current_ms += (int64_t)offset_min * 60000LL;
        }
    } else if (type == 16 && data_len >= 11 && ctx->summary_out != NULL) {
        ctx->stage_packets++;
        /* Big-endian, like the rest of the packet body (Gadgetbridge SleepDetailsParser). */
        const uint16_t sleep_duration = miwear_read_u16_be(d + 1);
        const uint16_t wake_duration = miwear_read_u16_be(d + 3);
        const uint16_t light_duration = miwear_read_u16_be(d + 5);
        const uint16_t rem_duration = miwear_read_u16_be(d + 7);
        const uint16_t deep_duration = miwear_read_u16_be(d + 9);
        ctx->summary_out->total_min = (int)sleep_duration;
        ctx->summary_out->awake_min = (int)wake_duration;
        ctx->summary_out->light_min = (int)light_duration;
        ctx->summary_out->rem_min = (int)rem_duration;
        ctx->summary_out->deep_min = (int)deep_duration;
        ctx->summary_out->valid = TRUE;
    }
    return TRUE;
}

static guint parse_sleep_feature_data(const uint8_t *payload,
                                      size_t end,
                                      size_t pos,
                                      uint32_t bed_sec,
                                      uint32_t wake_sec,
                                      GArray *samples,
                                      GArray *stage_rows,
                                      GArray *pulse_rows,
                                      SleepSummaryMinutes *summary_out) {
    if (pos >= end) {
        return 0;
    }
    ParseSleepFeatureCtx ctx = {
        .samples = samples,
        .stage_rows = stage_rows,
        .pulse_rows = pulse_rows,
        .summary_out = summary_out,
        .stage_packets = 0,
        .last_hb_ms = 0,
        .bed_sec = bed_sec,
        .wake_sec = wake_sec,
    };
    (void)miwear_sleep_foreach_packet(payload + pos, end - pos, parse_sleep_feature_packet, &ctx);
    return ctx.stage_packets;
}

static void finalize_sleep_parse(AppState *state,
                                 uint32_t ts,
                                 uint32_t bed_time,
                                 uint32_t wake_time,
                                 uint8_t sleep_full,
                                 GArray *samples,
                                 GArray *stage_rows,
                                 GArray *pulse_rows,
                                 guint stage_packets,
                                 size_t feature_len,
                                 const SleepSummaryMinutes *summary) {
    const int total_min = summary != NULL && summary->valid ? summary->total_min : -1;
    const int deep_min = summary != NULL && summary->valid ? summary->deep_min : -1;
    const int light_min = summary != NULL && summary->valid ? summary->light_min : -1;
    const int rem_min = summary != NULL && summary->valid ? summary->rem_min : -1;
    const int awake_min = summary != NULL && summary->valid ? summary->awake_min : -1;
    if (bed_time != 0 && wake_time != 0) {
        (void)miwear_db_replace_sleep_details_header((int64_t)bed_time * 1000LL,
                                                     (int64_t)wake_time * 1000LL,
                                                     (int)sleep_full,
                                                     total_min,
                                                     deep_min,
                                                     light_min,
                                                     rem_min,
                                                     awake_min);
    }
    if (stage_rows->len > 0) {
        (void)miwear_db_replace_sleep_stage_samples((const MiwearSleepStageRow *)stage_rows->data, stage_rows->len);
    }
    if (pulse_rows->len > 0) {
        (void)miwear_db_replace_heart_pulse_samples((const MiwearHeartPulseRow *)pulse_rows->data, pulse_rows->len);
    }

    if (samples->len > 0) {
        recorded_store_daily_details(state, ts, (const RecordedSample *)samples->data, samples->len);
        log_sleep_samples_summary(state, "Sleep details parsed", samples);
    }

    gchar *bed_txt = fmt_unix_ts(bed_time);
    gchar *wake_txt = fmt_unix_ts(wake_time);
    gchar *line = g_strdup_printf(
        "Sleep details: bed=%s wake=%s awakeFlag=%u featureBytes=%zu stagePackets=%u stagePoints=%u",
        bed_txt,
        wake_txt,
        sleep_full,
        feature_len,
        stage_packets,
        stage_rows->len);
    append_log(state, line);
    g_free(line);
    if (stage_rows->len > 0) {
        append_log(state, "Sleep stage timeline stored in dedicated Sleep chart.");
    } else if (summary != NULL && summary->valid) {
        gchar *sum_line = g_strdup_printf(
            "Sleep summary (type 16): total=%d deep=%d light=%d rem=%d awake=%d",
            summary->total_min,
            summary->deep_min,
            summary->light_min,
            summary->rem_min,
            summary->awake_min);
        append_log(state, sum_line);
        g_free(sum_line);
    } else {
        append_log(state, "Sleep stages: no hypnogram packets in featureData (check advanced sleep tracking).");
    }
    if (stage_rows->len > 0 || (summary != NULL && summary->valid) ||
        (bed_time != 0 && wake_time != 0 && wake_time > bed_time)) {
        recorded_schedule_rebuild(state);
    }
    g_free(bed_txt);
    g_free(wake_txt);
}

static void parse_full_day_sleep_report(AppState *state,
                                        uint32_t ts,
                                        int version,
                                        const uint8_t *payload,
                                        size_t payload_len) {
    const size_t crc_end = payload_len >= 4 ? payload_len - 4 : payload_len;
    size_t vf_len = 0;
    if (version == 5) {
        vf_len = 2;
    } else if (version == 6) {
        vf_len = 3;
    } else {
        append_log(state, "Full-day sleep parser: unsupported version.");
        return;
    }
    if (crc_end < 10 + 1) {
        append_log(state, "Full-day sleep parser: payload too short.");
        return;
    }

    const uint8_t *vf = payload + 8;
    size_t off = 8 + vf_len;
    const uint8_t sleep_full = payload[off++];

    uint32_t start_time = 0;
    uint32_t end_time = 0;
    uint32_t go_bed = 0;
    uint32_t live_bed = 0;

    if (miwear_full_day_sleep_valid_bit(vf, vf_len, 0) && off + 4 <= crc_end) {
        start_time = miwear_read_u32_le(payload + off);
        off += 4;
    }
    if (miwear_full_day_sleep_valid_bit(vf, vf_len, 1) && off + 4 <= crc_end) {
        end_time = miwear_read_u32_le(payload + off);
        off += 4;
    }
    if (miwear_full_day_sleep_valid_bit(vf, vf_len, 2) && off + 1 <= crc_end) {
        off += 1;
    }
    if (miwear_full_day_sleep_valid_bit(vf, vf_len, 3) && off + 1 <= crc_end) {
        off += 1;
    }
    if (miwear_full_day_sleep_valid_bit(vf, vf_len, 4) && off + 4 <= crc_end) {
        off += 4;
    }
    if (miwear_full_day_sleep_valid_bit(vf, vf_len, 5) && off + 4 <= crc_end) {
        off += 4;
    }
    if (miwear_full_day_sleep_valid_bit(vf, vf_len, 6) && off + 4 <= crc_end) {
        go_bed = miwear_read_u32_le(payload + off);
        off += 4;
    }
    if (miwear_full_day_sleep_valid_bit(vf, vf_len, 7) && off + 4 <= crc_end) {
        live_bed = miwear_read_u32_le(payload + off);
        off += 4;
    }

    GArray *samples = g_array_new(FALSE, FALSE, sizeof(RecordedSample));
    GArray *stage_rows = g_array_new(FALSE, FALSE, sizeof(MiwearSleepStageRow));
    GArray *pulse_rows = g_array_new(FALSE, FALSE, sizeof(MiwearHeartPulseRow));

    const uint32_t bed_time = go_bed != 0 ? go_bed : start_time;
    const uint32_t wake_time = live_bed != 0 ? live_bed : end_time;

    if (miwear_full_day_sleep_valid_bit(vf, vf_len, 8) && off + 4 <= crc_end) {
        const uint16_t unit = miwear_read_u16_le(payload + off);
        off += 2;
        const uint16_t count = miwear_read_u16_le(payload + off);
        off += 2;
        off = parse_sleep_hr_series(samples, payload, payload_len, off, bed_time, unit, count);
    }
    if (miwear_full_day_sleep_valid_bit(vf, vf_len, 9) && off + 4 <= crc_end) {
        const uint16_t unit = miwear_read_u16_le(payload + off);
        off += 2;
        const uint16_t count = miwear_read_u16_le(payload + off);
        off += 2;
        off = parse_sleep_spo2_series(samples, payload, payload_len, off, bed_time, unit, count);
    }
    if (miwear_full_day_sleep_valid_bit(vf, vf_len, 10) && off + 4 <= crc_end) {
        off += 2; /* snore interval */
        const uint16_t count = miwear_read_u16_le(payload + off);
        off += 2;
        if (count > 0 && off + 4 <= crc_end) {
            off += 4; /* snoreFirstTime */
        }
        const size_t skip = (size_t)count * 4u;
        if (off + skip <= crc_end) {
            off += skip;
        } else {
            off = crc_end;
        }
    }

    const size_t feature_off = miwear_full_day_sleep_structured_end(payload, payload_len, version);
    const size_t feature_len =
        feature_off > 0 && feature_off < crc_end ? crc_end - feature_off : 0;
    SleepSummaryMinutes summary = {0};
    guint stage_packets = 0;
    if (feature_len > 0) {
        stage_packets = parse_sleep_feature_data(payload,
                                                 crc_end,
                                                 feature_off,
                                                 bed_time,
                                                 wake_time,
                                                 samples,
                                                 stage_rows,
                                                 pulse_rows,
                                                 &summary);
    }

    finalize_sleep_parse(state,
                         ts,
                         bed_time,
                         wake_time,
                         sleep_full,
                         samples,
                         stage_rows,
                         pulse_rows,
                         stage_packets,
                         feature_len,
                         &summary);

    g_array_unref(samples);
    g_array_unref(stage_rows);
    g_array_unref(pulse_rows);
}

static void parse_sleep_details_for_log(AppState *state,
                                        uint32_t ts,
                                        int version,
                                        const uint8_t *payload,
                                        size_t payload_len) {
    if (version >= 5) {
        parse_full_day_sleep_report(state, ts, version, payload, payload_len);
        return;
    }

    int header_size = 0;
    if (version >= 1 && version <= 4) {
        header_size = 1;
    } else {
        append_log(state, "Sleep details parser: unsupported version.");
        return;
    }
    if (payload_len < (size_t)(8 + header_size + 9 + 4)) {
        append_log(state, "Sleep details parser: payload too short.");
        return;
    }

    size_t off = 8;
    const uint8_t *header = payload + off;
    off += (size_t)header_size;
    int header_idx = 0;

    const uint8_t is_awake = payload[off++]; header_idx++;
    const uint32_t bed_time = miwear_read_u32_le(payload + off); off += 4; header_idx++;
    const uint32_t wake_time = miwear_read_u32_le(payload + off); off += 4; header_idx++;
    if (version >= 4) {
        if (sleep_header_valid_bit(header, (size_t)header_size, header_idx) && off < payload_len - 4) {
            off += 1;
        }
        header_idx++;
    }

    GArray *samples = g_array_new(FALSE, FALSE, sizeof(RecordedSample));
    GArray *stage_rows = g_array_new(FALSE, FALSE, sizeof(MiwearSleepStageRow));
    GArray *pulse_rows = g_array_new(FALSE, FALSE, sizeof(MiwearHeartPulseRow));

    if (sleep_header_valid_bit(header, (size_t)header_size, header_idx) && off + 4 <= payload_len - 4) {
        uint16_t unit = miwear_read_u16_le(payload + off); off += 2;
        uint16_t count = miwear_read_u16_le(payload + off); off += 2;
        uint32_t first_record = bed_time;
        if (count > 0 && version >= 2 && off + 4 <= payload_len - 4) {
            first_record = miwear_read_u32_le(payload + off);
            off += 4;
        }
        for (uint16_t i = 0; i < count && off < payload_len - 4; i++) {
            uint8_t hr = payload[off++];
            if (hr > 0 && hr < 240) {
                uint32_t sec = first_record + (uint32_t)unit * (uint32_t)i;
                RecordedSample s = {0};
                s.minute_of_day = local_minute_of_day((gint64)sec);
                s.steps = s.active_calories = s.distance_cm = s.energy = s.spo2 = s.stress = s.sleep_stage = -1;
                s.heart_rate = hr;
                s.sleep_source = 1;
                g_array_append_val(samples, s);
            }
        }
    }
    header_idx++;

    if (sleep_header_valid_bit(header, (size_t)header_size, header_idx) && off + 4 <= payload_len - 4) {
        uint16_t unit = miwear_read_u16_le(payload + off); off += 2;
        uint16_t count = miwear_read_u16_le(payload + off); off += 2;
        uint32_t first_record = bed_time;
        if (count > 0 && version >= 2 && off + 4 <= payload_len - 4) {
            first_record = miwear_read_u32_le(payload + off);
            off += 4;
        }
        for (uint16_t i = 0; i < count && off < payload_len - 4; i++) {
            uint8_t spo2 = payload[off++];
            if (spo2 > 0 && spo2 <= 100) {
                uint32_t sec = first_record + (uint32_t)unit * (uint32_t)i;
                RecordedSample s = {0};
                s.minute_of_day = local_minute_of_day((gint64)sec);
                s.steps = s.active_calories = s.distance_cm = s.energy = s.heart_rate = s.stress = s.sleep_stage = -1;
                s.spo2 = spo2;
                s.sleep_source = 1;
                g_array_append_val(samples, s);
            }
        }
    }
    header_idx++;

    if (version >= 3) {
        if (sleep_header_valid_bit(header, (size_t)header_size, header_idx) && off + 4 <= payload_len - 4) {
            off += 2; /* unit */
            uint16_t count = miwear_read_u16_le(payload + off); off += 2;
            if (count > 0 && version >= 2 && off + 4 <= payload_len - 4) {
                off += 4;
            }
            size_t skip = (size_t)count * 4u;
            if (off + skip <= payload_len - 4) off += skip;
        }
        header_idx++;
    }

    const size_t crc_end = payload_len >= 4 ? payload_len - 4 : payload_len;
    const size_t feature_len = off < crc_end ? crc_end - off : 0;
    SleepSummaryMinutes summary = {0};
    const guint stage_packets = parse_sleep_feature_data(payload,
                                                         crc_end,
                                                         off,
                                                         bed_time,
                                                         wake_time,
                                                         samples,
                                                         stage_rows,
                                                         pulse_rows,
                                                         &summary);

    finalize_sleep_parse(state,
                         ts,
                         bed_time,
                         wake_time,
                         is_awake,
                         samples,
                         stage_rows,
                         pulse_rows,
                         stage_packets,
                         feature_len,
                         &summary);

    g_array_unref(samples);
    g_array_unref(stage_rows);
    g_array_unref(pulse_rows);
}

const char *activity_subtype_name(int type, int subtype) {
    if (type == 0) {
        switch (subtype) {
            case 0x00: return "ACTIVITY_DAILY";
            case 0x02: return "DAYTIME_SLEEP";
            case 0x03: return "SLEEP_STAGES";
            case 0x06: return "MANUAL_SAMPLES";
            case 0x08: return "SLEEP";
            case 0x09: return "ABNORMAL";
            case 0x0a: return "WEIGHT";
            case 0x0b: return "ECG";
            case 0x0c: return "TEMPERATURE";
            default: return "ACTIVITY_UNKNOWN";
        }
    }
    if (type == 1) {
        return miwear_sport_subtype_name(subtype);
    }
    return "UNKNOWN";
}
void handle_activity_payload(AppState *state, const uint8_t *payload, size_t payload_len) {
    const uint8_t *file_id = NULL; /* first 7 bytes */
    gboolean ack_allowed = FALSE;
    gboolean advance_fetch_queue = FALSE;

    if (payload_len >= 7) {
        file_id = payload;
        if (state->activity_fetch_inflight &&
            memcmp(file_id, state->activity_fetch_current_id, 7) == 0) {
            advance_fetch_queue = TRUE;
        }
    } else if (state->activity_fetch_inflight) {
        /*
         * Corrupted payload with no recoverable file id - avoid deadlocking the fetch queue.
         * This mirrors GB's behavior of moving to the next fetch on malformed payloads.
         */
        advance_fetch_queue = TRUE;
    }

    if (payload_len < 13) {
        append_log(state, "Activity payload too short.");
        goto finish;
    }

    const uint32_t actual_crc = crc32_ieee(payload, payload_len - 4);
    const uint32_t expected_crc = miwear_read_u32_le(payload + payload_len - 4);
    if (actual_crc != expected_crc) {
        gchar *line = g_strdup_printf("Activity payload CRC mismatch: got=%08X expected=%08X", actual_crc, expected_crc);
        append_log(state, line);
        g_free(line);
        goto finish;
    }
    ack_allowed = TRUE;

    (void)miwear_db_ensure_device(state->selected_address);

    const uint32_t ts = miwear_read_u32_le(payload);
    const int8_t tz = (int8_t)payload[4];
    const int version = payload[5];
    const int flags = payload[6];
    const int type = (flags >> 7) & 1;
    const int subtype = (flags & 127) >> 2;
    const int detail = flags & 3;

    const char *name = activity_subtype_name(type, subtype);
    gchar *line = g_strdup_printf("Activity payload: ts=%u tz=%d type=%d subtype=%d(%s) detail=%d ver=%d bytes=%zu (CRC ok)",
                                  ts, tz, type, subtype, name, detail, version, payload_len);
    append_log(state, line);
    g_free(line);

    if (type == 1) {
        miwear_apply_workout_file(state, ts, subtype, detail, version, payload, payload_len);
    } else if (type == 0 && subtype == 0 && detail == 0) {
        parse_daily_details_for_log(state, ts, version, payload, payload_len);
    } else if (type == 0 && subtype == 0 && detail == 1) {
        parse_daily_summary_for_log(state, ts, tz, version, payload, payload_len);
    } else if (type == 0 && subtype == 3) {
        if (version == 2) {
            parse_sleep_stages_v2_for_log(state, ts, payload, payload_len);
        }
        mi_fitness_apply_health_payload(state, ts, tz, subtype, detail, version, payload, payload_len);
    } else if (type == 0 && subtype == 8) {
        parse_sleep_details_for_log(state, ts, version, payload, payload_len);
        mi_fitness_apply_health_payload(state, ts, tz, subtype, detail, version, payload, payload_len);
    } else if (type == 0 && subtype == 6) {
        miwear_manual_entries_reset();
        miwear_apply_manual_samples(state, ts, payload, payload_len);
        mi_fitness_apply_health_payload(state, ts, tz, subtype, detail, version, payload, payload_len);
        if (version >= 2 && miwear_manual_entries_committed() == 0) {
            append_log(state,
                       "Manual samples (v2): no stress/SpO₂/HR extracted — not acking so the band can retry.");
            ack_allowed = FALSE;
        }
    } else if (type == 0 && (subtype == 9 || subtype == 10 || subtype == 11 || subtype == 12)) {
        mi_fitness_apply_health_payload(state, ts, tz, subtype, detail, version, payload, payload_len);
    } else if (type == 0) {
        mi_fitness_apply_health_payload(state, ts, tz, subtype, detail, version, payload, payload_len);
    } else {
        gchar *line = g_strdup_printf("Unhandled fitness file type=%d subtype=%d detail=%d",
                                      type, subtype, detail);
        append_log(state, line);
        g_free(line);
    }

finish:
    if (state->auth_initialized && ack_allowed && file_id != NULL) {
        (void)send_activity_fetch_ack(state, file_id, 7);
        if (state->activity_fetch_inflight &&
            memcmp(file_id, state->activity_fetch_current_id, 7) != 0) {
            miwear_log_fetch_transition(state, "ack-sidecar", file_id);
        } else {
            miwear_log_fetch_transition(state, "ack", file_id);
        }
    }

    if (advance_fetch_queue && state->activity_fetch_inflight) {
        miwear_log_fetch_transition(state, ack_allowed ? "advance-next" : "drop-next", file_id);
        if (state->activity_fetch_timeout_source_id != 0) {
            g_source_remove(state->activity_fetch_timeout_source_id);
            state->activity_fetch_timeout_source_id = 0;
        }
        state->activity_fetch_inflight = FALSE;
        memset(state->activity_fetch_current_id, 0, sizeof(state->activity_fetch_current_id));
        trigger_next_activity_fetch(state);
    } else if (state->activity_fetch_inflight &&
               ack_allowed &&
               file_id != NULL &&
               memcmp(file_id, state->activity_fetch_current_id, 7) != 0) {
        miwear_log_fetch_transition(state, "waiting-current", state->activity_fetch_current_id);
    }
}
