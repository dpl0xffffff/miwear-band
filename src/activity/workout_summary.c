#include "miwear/activity/workout_summary.h"
#include "miwear/activity/mi_fitness_sport_report.h"
#include "miwear/activity/mi_fitness_sport_tables.h"
#include "miwear/activity/mi_fitness_dispatch.h"
#include "miwear/activity/schema_reader.h"
#include "miwear/ui/dashboard.h"
#include "miwear/app.h"
#include "miwear/util.h"

#include <string.h>

typedef struct {
    const uint8_t *buf;
    size_t off;
    size_t end;
} MiReader;

static gboolean mi_need(MiReader *r, size_t n) {
    return r->off + n <= r->end;
}

static gboolean mi_u8(MiReader *r, uint8_t *v) {
    if (!mi_need(r, 1)) {
        return FALSE;
    }
    *v = r->buf[r->off++];
    return TRUE;
}

static gboolean mi_u16(MiReader *r, uint16_t *v) {
    if (!mi_need(r, 2)) {
        return FALSE;
    }
    *v = (uint16_t)miwear_read_u16_le(r->buf + r->off);
    r->off += 2;
    return TRUE;
}

static gboolean mi_u32(MiReader *r, uint32_t *v) {
    if (!mi_need(r, 4)) {
        return FALSE;
    }
    *v = miwear_read_u32_le(r->buf + r->off);
    r->off += 4;
    return TRUE;
}

static gboolean mi_f32(MiReader *r, float *v) {
    if (!mi_need(r, 4)) {
        return FALSE;
    }
    uint32_t raw = miwear_read_u32_le(r->buf + r->off);
    memcpy(v, &raw, sizeof(float));
    r->off += 4;
    return TRUE;
}

static gboolean mi_skip(MiReader *r, size_t n) {
    if (!mi_need(r, n)) {
        return FALSE;
    }
    r->off += n;
    return TRUE;
}

static size_t workout_data_start(const uint8_t *payload, size_t payload_len) {
    size_t off = 7;
    const size_t body_end = payload_len >= 4 ? payload_len - 4 : 0;
    if (off < body_end && payload[off] != 0) {
        off++;
    }
    return off;
}

static gboolean mi_reader_init(MiReader *r, const uint8_t *payload, size_t payload_len, size_t header_size) {
    const size_t start = workout_data_start(payload, payload_len);
    const size_t body_end = payload_len >= 4 ? payload_len - 4 : 0;
    if (start + header_size > body_end) {
        return FALSE;
    }
    r->buf = payload;
    r->off = start + header_size;
    r->end = body_end;
    return TRUE;
}

static void fill_core(MiwearWorkoutSummary *out,
                      uint32_t start,
                      uint32_t end,
                      uint32_t active_sec,
                      int distance_m,
                      int calories,
                      int hr_avg,
                      int hr_max,
                      int hr_min) {
    out->start_sec = start;
    out->end_sec = end;
    if (active_sec > 0 && end > start && active_sec > (end - start) + 300u) {
        active_sec = end - start;
    }
    out->duration_sec = active_sec > 0 ? (int)active_sec : (end > start ? (int)(end - start) : -1);
    out->distance_m = distance_m;
    out->calories = calories;
    out->hr_avg = hr_avg;
    out->hr_max = hr_max;
    out->hr_min = hr_min;
}

static gboolean summary_values_sane(const MiwearWorkoutSummary *s) {
    if (s->start_sec == 0 || s->end_sec == 0 || s->end_sec <= s->start_sec) {
        return FALSE;
    }
    const uint32_t span = s->end_sec - s->start_sec;
    if (span > 8u * 3600u) {
        return FALSE;
    }
    if (s->duration_sec <= 0 || s->duration_sec > (int)(span + 300u) || s->duration_sec > 8 * 3600) {
        return FALSE;
    }
    if (s->distance_m > 100000 || s->calories > 5000) {
        return FALSE;
    }
    if (s->duration_sec > 0 && s->calories > 0) {
        /* Rough upper bound: ~20 kcal/min is already a hard workout. */
        const int max_kcal = (s->duration_sec / 60) * 20 + 30;
        if (s->calories > max_kcal) {
            return FALSE;
        }
    }
    if (s->hr_max > 0 && s->hr_min > 0 && s->hr_min > s->hr_max) {
        return FALSE;
    }
    if (s->hr_max > 220 || s->hr_min > 220) {
        return FALSE;
    }
    return TRUE;
}

static gboolean heuristic_walk_summary(const uint8_t *payload,
                                       size_t payload_len,
                                       uint32_t file_ts_sec,
                                       MiwearWorkoutSummary *out) {
    const size_t body_end = payload_len >= 4 ? payload_len - 4 : 0;
    const size_t base = workout_data_start(payload, payload_len);

    for (size_t off = base; off + 28 <= body_end; off++) {
        const uint32_t start = miwear_read_u32_le(payload + off);
        const uint32_t end = miwear_read_u32_le(payload + off + 4);
        const uint32_t active = miwear_read_u32_le(payload + off + 8);
        if (end <= start || end - start > 8u * 3600u) {
            continue;
        }
        if (active < 30 || active > end - start + 300u) {
            continue;
        }
        if (file_ts_sec != 0 && (start > file_ts_sec + 7200u || start + 7200u < file_ts_sec)) {
            continue;
        }
        const uint32_t dist = miwear_read_u32_le(payload + off + 16);
        /* Gadgetbridge outdoor walk v2: total @+20, active burnt @+22 (after start u32). */
        const uint16_t kcal_total = (uint16_t)miwear_read_u16_le(payload + off + 20);
        const uint16_t kcal_burnt = (uint16_t)miwear_read_u16_le(payload + off + 22);
        uint16_t kcal = kcal_burnt > 0 ? kcal_burnt : kcal_total;
        if (dist > 100000u || kcal > 5000u) {
            continue;
        }
        uint8_t hr_avg = 0, hr_max = 0, hr_min = 0;
        if (off + 57 <= body_end) {
            hr_avg = payload[off + 54];
            hr_max = payload[off + 55];
            hr_min = payload[off + 56];
        }
        fill_core(out, start, end, active, (int)dist, (int)kcal, (int)hr_avg, (int)hr_max, (int)hr_min);
        if (summary_values_sane(out)) {
            return TRUE;
        }
    }
    return FALSE;
}

static gboolean parse_freestyle(MiReader *r, int version, MiwearWorkoutSummary *out) {
    uint32_t start = 0, end = 0, active = 0;
    uint16_t kcal = 0;
    uint8_t hr_avg = 0, hr_max = 0, hr_min = 0;
    float te_aerobic = 0;

    if (!mi_u32(r, &start) || !mi_u32(r, &end) || !mi_u32(r, &active) || !mi_u16(r, &kcal)) {
        return FALSE;
    }
    if (!mi_u8(r, &hr_avg) || !mi_u8(r, &hr_max) || !mi_u8(r, &hr_min)) {
        return FALSE;
    }
    if (version > 5 && !mi_skip(r, 6)) {
        return FALSE;
    }
    if (!mi_f32(r, &te_aerobic)) {
        return FALSE;
    }
    (void)te_aerobic;
    if (version > 6 && !mi_skip(r, 1)) {
        return FALSE;
    }
    if (!mi_skip(r, 1)) {
        return FALSE;
    }
    if (!mi_skip(r, 2)) {
        return FALSE;
    }
    if (!mi_skip(r, 4 * 5)) {
        return FALSE;
    }
    if (version == 5) {
        if (!mi_skip(r, 10 + 2 + 2 + 4 + 2)) {
            return FALSE;
        }
    } else {
        if (!mi_skip(r, 2 + 4)) {
            return FALSE;
        }
        if (!mi_f32(r, &te_aerobic)) {
            return FALSE;
        }
        if (!mi_skip(r, 1 + 2 + 2 + 4 + 2 + 2 + 1)) {
            return FALSE;
        }
        if (version > 7 && !mi_skip(r, 1)) {
            return FALSE;
        }
    }
    fill_core(out, start, end, active, -1, (int)kcal, (int)hr_avg, (int)hr_max, (int)hr_min);
    return TRUE;
}

static gboolean parse_outdoor_run_v1(MiReader *r, MiwearWorkoutSummary *out) {
    uint32_t start = 0, end = 0, active = 0, dist = 0, kcal = 0, steps = 0;
    uint8_t hr_avg = 0, hr_max = 0, hr_min = 0;
    float recovery = 0;

    if (!mi_u32(r, &start) || !mi_u32(r, &end) || !mi_u32(r, &active) || !mi_u32(r, &dist) ||
        !mi_u32(r, &kcal)) {
        return FALSE;
    }
    if (!mi_skip(r, 4 + 4)) {
        return FALSE;
    }
    if (!mi_u32(r, &steps)) {
        return FALSE;
    }
    (void)steps;
    if (!mi_skip(r, 2)) {
        return FALSE;
    }
    if (!mi_u8(r, &hr_avg) || !mi_u8(r, &hr_max) || !mi_u8(r, &hr_min)) {
        return FALSE;
    }
    if (!mi_skip(r, 20)) {
        return FALSE;
    }
    if (!mi_f32(r, &recovery)) {
        return FALSE;
    }
    (void)recovery;
    if (!mi_skip(r, 9 + 1 + 2 + 4 * 5 + 4)) {
        return FALSE;
    }
    fill_core(out, start, end, active, (int)dist, (int)kcal, (int)hr_avg, (int)hr_max, (int)hr_min);
    return TRUE;
}

static gboolean parse_outdoor_walk_v2(MiReader *r, int version, MiwearWorkoutSummary *out) {
    uint16_t workout_type = 0;
    uint32_t start = 0, end = 0, active = 0, dist = 0, steps = 0;
    uint32_t pace_avg = 0, pace_max = 0, pace_min = 0;
    float speed_avg = 0.0f, speed_max = 0.0f;
    uint16_t kcal_total = 0, kcal = 0;
    uint16_t step_len = 0, step_rate_avg = 0, step_rate_max = 0;
    uint8_t hr_avg = 0, hr_max = 0, hr_min = 0;
    float te_aerobic = 0.0f, te_anaerobic = 0.0f;

    if (!mi_u16(r, &workout_type) || !mi_u32(r, &start) || !mi_u32(r, &end) || !mi_u32(r, &active) ||
        !mi_skip(r, 4) || !mi_u32(r, &dist) || !mi_u16(r, &kcal_total) || !mi_u16(r, &kcal)) {
        return FALSE;
    }
    if (version >= 5 && !mi_u32(r, &pace_avg)) {
        return FALSE;
    }
    if (!mi_u32(r, &pace_max) || !mi_u32(r, &pace_min)) {
        return FALSE;
    }
    if (version >= 5 && !mi_f32(r, &speed_avg)) {
        return FALSE;
    }
    if (!mi_f32(r, &speed_max) || !mi_u32(r, &steps)) {
        return FALSE;
    }
    if (version >= 5 && (!mi_u16(r, &step_len) || !mi_u16(r, &step_rate_avg))) {
        return FALSE;
    }
    if (!mi_u16(r, &step_rate_max) || !mi_u8(r, &hr_avg) || !mi_u8(r, &hr_max) || !mi_u8(r, &hr_min)) {
        return FALSE;
    }
    if (version == 1) {
        if (!mi_skip(r, 33)) {
            return FALSE;
        }
    } else {
        if (!mi_skip(r, 20) || !mi_f32(r, &te_aerobic) || !mi_skip(r, 1) || !mi_f32(r, &te_anaerobic)) {
            return FALSE;
        }
        if (version >= 9) {
            if (!mi_skip(r, 6 + 1 + 2)) {
                return FALSE;
            }
        } else if (!mi_skip(r, 4)) {
            return FALSE;
        }
        if (!mi_skip(r, 2 + 1)) {
            return FALSE;
        }
    }
    if (!mi_skip(r, 4 * 5)) {
        return FALSE;
    }
    (void)workout_type;
    (void)pace_avg;
    (void)pace_max;
    (void)pace_min;
    (void)speed_avg;
    (void)speed_max;
    (void)steps;
    (void)step_len;
    (void)step_rate_avg;
    (void)step_rate_max;
    (void)kcal_total;
    (void)te_aerobic;
    (void)te_anaerobic;
    fill_core(out, start, end, active, (int)dist, (int)kcal, (int)hr_avg, (int)hr_max, (int)hr_min);
    return TRUE;
}

static gboolean parse_hiit(MiReader *r, MiwearWorkoutSummary *out) {
    uint32_t start = 0, end = 0, active = 0;
    uint16_t kcal = 0;
    uint8_t hr_avg = 0, hr_max = 0, hr_min = 0;
    float te = 0;

    if (!mi_u32(r, &start) || !mi_u32(r, &end) || !mi_u32(r, &active) || !mi_u16(r, &kcal)) {
        return FALSE;
    }
    if (!mi_u8(r, &hr_avg) || !mi_u8(r, &hr_max) || !mi_u8(r, &hr_min) || !mi_f32(r, &te)) {
        return FALSE;
    }
    (void)te;
    if (!mi_skip(r, 1 + 1 + 2 + 4 * 5)) {
        return FALSE;
    }
    fill_core(out, start, end, active, -1, (int)kcal, (int)hr_avg, (int)hr_max, (int)hr_min);
    return TRUE;
}

static gboolean parse_elliptical(MiReader *r, int version, MiwearWorkoutSummary *out) {
    uint32_t start = 0, end = 0, active = 0, steps = 0;
    uint16_t kcal = 0, cadence_max = 0;
    uint8_t hr_avg = 0, hr_max = 0, hr_min = 0;

    if (!mi_u32(r, &start) || !mi_u32(r, &end) || !mi_u32(r, &active) || !mi_u16(r, &kcal) ||
        !mi_u32(r, &steps)) {
        return FALSE;
    }
    (void)steps;
    if (version >= 6 && !mi_skip(r, 2)) {
        return FALSE;
    }
    if (!mi_u16(r, &cadence_max)) {
        return FALSE;
    }
    (void)cadence_max;
    if (!mi_u8(r, &hr_avg) || !mi_u8(r, &hr_max) || !mi_u8(r, &hr_min) || !mi_skip(r, 7)) {
        return FALSE;
    }
    if (version >= 4 && !mi_skip(r, 1)) {
        return FALSE;
    }
    if (!mi_skip(r, 4 * 5)) {
        return FALSE;
    }
    fill_core(out, start, end, active, -1, (int)kcal, (int)hr_avg, (int)hr_max, (int)hr_min);
    return TRUE;
}

static int header_size_for(int subtype, int version) {
    switch (subtype) {
        case 0x08:
            if (version == 5) {
                return 3;
            }
            if (version == 7) {
                return 5;
            }
            if (version >= 8 && version <= 10) {
                return 6;
            }
            return -1;
        case 0x01:
            if (version == 4) {
                return 4;
            }
            return -1;
        case 0x16:
            if (version == 1) {
                return 5;
            }
            if (version == 4) {
                return 7;
            }
            if (version == 5 || version == 6) {
                return 9;
            }
            if (version == 9) {
                return 13;
            }
            return -1;
        case 0x10:
            if (version == 5) {
                return 4;
            }
            return -1;
        case 0x0b:
            if (version >= 3 && version <= 6) {
                return 4;
            }
            return -1;
        default:
            return -1;
    }
}

static gboolean parse_with_header(const uint8_t *payload,
                                  size_t payload_len,
                                  int subtype,
                                  int version,
                                  int header_size,
                                  MiwearWorkoutSummary *out) {
    MiReader r;
    if (!mi_reader_init(&r, payload, payload_len, (size_t)header_size)) {
        return FALSE;
    }
    switch (subtype) {
        case 0x08:
            return parse_freestyle(&r, version, out);
        case 0x01:
            return parse_outdoor_run_v1(&r, out);
        case 0x16:
            return parse_outdoor_walk_v2(&r, version, out);
        case 0x10:
            return parse_hiit(&r, out);
        case 0x0b:
            return parse_elliptical(&r, version, out);
        default:
            return FALSE;
    }
}

static gboolean try_schema_workout_summary(const uint8_t *payload,
                                           size_t payload_len,
                                           int subtype,
                                           int version,
                                           MiwearWorkoutSummary *out) {
    gchar *rel = miwear_schema_resolve_sport(subtype, 1, version);
    if (rel == NULL) {
        return FALSE;
    }
    gchar *schema_path = g_build_filename(miwear_schema_dir(), rel, NULL);
    g_free(rel);
    MiwearWorkoutSummary trial = {0};
    trial.sport_subtype = subtype;
    trial.version = version;
    trial.sport_name = g_strdup(miwear_sport_subtype_name(subtype));
    const gboolean ok = miwear_schema_parse_sport_report(schema_path, payload, payload_len, &trial);
    g_free(schema_path);
    if (!ok) {
        miwear_workout_summary_clear(&trial);
        return FALSE;
    }
    *out = trial;
    return TRUE;
}

gboolean miwear_parse_workout_summary(const uint8_t *payload,
                                      size_t payload_len,
                                      int subtype,
                                      int version,
                                      uint32_t file_ts_sec,
                                      MiwearWorkoutSummary *out) {
    if (payload == NULL || out == NULL || payload_len < 13) {
        return FALSE;
    }

    miwear_workout_summary_clear(out);

    if (try_schema_workout_summary(payload, payload_len, subtype, version, out)) {
        return TRUE;
    }

    {
        MiwearWorkoutSummary trial = {0};
        trial.sport_subtype = subtype;
        trial.version = version;
        trial.sport_name = g_strdup(miwear_sport_subtype_name(subtype));
        if (mi_fitness_parse_sport_report(payload, payload_len, subtype, version, &trial) && summary_values_sane(&trial)) {
            *out = trial;
            return TRUE;
        }
        miwear_workout_summary_clear(&trial);
    }

    const int default_header = header_size_for(subtype, version);
    int headers[4];
    size_t n_headers = 0;
    if (subtype == 0x16 && (version == 5 || version == 6)) {
        headers[n_headers++] = 7;
        headers[n_headers++] = 9;
        headers[n_headers++] = 13;
    } else if (default_header >= 0) {
        headers[n_headers++] = default_header;
    } else {
        return FALSE;
    }

    for (size_t i = 0; i < n_headers; i++) {
        MiwearWorkoutSummary trial = {0};
        trial.sport_subtype = subtype;
        trial.version = version;
        trial.sport_name = g_strdup(miwear_sport_subtype_name(subtype));
        if (!parse_with_header(payload, payload_len, subtype, version, headers[i], &trial)) {
            miwear_workout_summary_clear(&trial);
            continue;
        }
        if (summary_values_sane(&trial)) {
            *out = trial;
            return TRUE;
        }
        miwear_workout_summary_clear(&trial);
    }

    if (subtype == 0x16 && heuristic_walk_summary(payload, payload_len, file_ts_sec, out)) {
        out->sport_subtype = subtype;
        out->version = version;
        out->sport_name = g_strdup(miwear_sport_subtype_name(subtype));
        return TRUE;
    }

    return FALSE;
}

void miwear_apply_workout_file(AppState *state,
                               uint32_t file_ts,
                               int subtype,
                               int detail,
                               int version,
                               const uint8_t *payload,
                               size_t payload_len) {
    if (state == NULL || payload == NULL) {
        return;
    }

    miwear_schema_maybe_capture(payload, payload_len, subtype, detail, version, file_ts);

    if (detail == 2) {
        gchar *line = g_strdup_printf("Workout GPS track (%s, decode TBD)", mi_fitness_sport_report_parser_name(subtype));
        append_log(state, line);
        g_free(line);
        return;
    }
    if (detail == 0) {
        gchar *line = g_strdup_printf("Workout time-series record (%s, schema/native TBD)",
                                      mi_fitness_sport_record_parser_name(subtype));
        append_log(state, line);
        g_free(line);
        miwear_sport_add_entry(state, file_ts, 1, subtype, NULL);
        return;
    }
    if (detail == 3) {
        append_log(state, "Workout HRR blob received (decode TBD).");
        return;
    }

    MiwearWorkoutSummary sum = {0};
    sum.file_ts_sec = file_ts;
    if (miwear_parse_workout_summary(payload, payload_len, subtype, version, file_ts, &sum)) {
        sum.file_ts_sec = file_ts;
        miwear_sport_add_entry(state, file_ts, 1, subtype, &sum);
        miwear_dashboard_refresh(state);
        gchar *line = g_strdup_printf(
            "Workout summary: %s v%d — %d min, %d m, %d kcal, HR %d/%d/%d",
            sum.sport_name != NULL ? sum.sport_name : "Workout",
            version,
            sum.duration_sec > 0 ? sum.duration_sec / 60 : 0,
            sum.distance_m > 0 ? sum.distance_m : 0,
            sum.calories > 0 ? sum.calories : 0,
            sum.hr_avg > 0 ? sum.hr_avg : 0,
            sum.hr_max > 0 ? sum.hr_max : 0,
            sum.hr_min > 0 ? sum.hr_min : 0);
        append_log(state, line);
        g_free(line);
    } else {
        gchar *line = g_strdup_printf("Workout summary: %s v%d — parser not ported yet (%zu bytes)",
                                      miwear_sport_subtype_name(subtype),
                                      version,
                                      payload_len);
        append_log(state, line);
        g_free(line);
        miwear_sport_add_entry(state, file_ts, 1, subtype, NULL);
    }
    miwear_workout_summary_clear(&sum);
}
