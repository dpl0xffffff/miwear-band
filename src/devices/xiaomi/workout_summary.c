#include "salus/devices/xiaomi/workout_summary.h"
#include "salus/devices/xiaomi/mi_fitness_report_tables.h"
#include "salus/devices/xiaomi/workout_record.h"
#include "salus/devices/xiaomi/mi_fitness_sport_report.h"
#include "salus/devices/xiaomi/mi_fitness_dispatch.h"
#include "salus/devices/xiaomi/schema_reader.h"
#include "salus/ui/dashboard.h"
#include "salus/app.h"
#include "salus/util.h"

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
    *v = (uint16_t)salus_read_u16_le(r->buf + r->off);
    r->off += 2;
    return TRUE;
}

static gboolean mi_u32(MiReader *r, uint32_t *v) {
    if (!mi_need(r, 4)) {
        return FALSE;
    }
    *v = salus_read_u32_le(r->buf + r->off);
    r->off += 4;
    return TRUE;
}

static gboolean mi_f32(MiReader *r, float *v) {
    if (!mi_need(r, 4)) {
        return FALSE;
    }
    uint32_t raw = salus_read_u32_le(r->buf + r->off);
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

static void fill_core(SalusWorkoutSummary *out,
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

static gboolean summary_values_sane(const SalusWorkoutSummary *s) {
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

static void finalize_walk_summary(int subtype, SalusWorkoutSummary *out) {
    if (out == NULL || (subtype != 0x16 && subtype != 2)) {
        return;
    }
    if (!summary_values_sane(out)) {
        return;
    }
    if (out->walk_avg_pace_sec_per_km <= 0 && out->distance_m > 0 && out->duration_sec > 0) {
        out->walk_avg_pace_sec_per_km = (int)((uint64_t)out->duration_sec * 1000u / (uint32_t)out->distance_m);
    }
    out->has_walk_detail = TRUE;
}

static gboolean heuristic_walk_summary(const uint8_t *payload,
                                       size_t payload_len,
                                       uint32_t file_ts_sec,
                                       SalusWorkoutSummary *out) {
    const size_t body_end = payload_len >= 4 ? payload_len - 4 : 0;
    const size_t base = workout_data_start(payload, payload_len);

    for (size_t off = base; off + 28 <= body_end; off++) {
        const uint32_t start = salus_read_u32_le(payload + off);
        const uint32_t end = salus_read_u32_le(payload + off + 4);
        const uint32_t active = salus_read_u32_le(payload + off + 8);
        if (end <= start || end - start > 8u * 3600u) {
            continue;
        }
        if (active < 30 || active > end - start + 300u) {
            continue;
        }
        if (file_ts_sec != 0 && (start > file_ts_sec + 7200u || start + 7200u < file_ts_sec)) {
            continue;
        }
        const uint32_t dist = salus_read_u32_le(payload + off + 16);
        /* Gadgetbridge outdoor walk v2: total @+20, active burnt @+22 (after start u32). */
        const uint16_t kcal_total = (uint16_t)salus_read_u16_le(payload + off + 20);
        const uint16_t kcal_burnt = (uint16_t)salus_read_u16_le(payload + off + 22);
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

static gboolean parse_freestyle(MiReader *r, int version, SalusWorkoutSummary *out) {
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

static gboolean parse_outdoor_run_v1(MiReader *r, SalusWorkoutSummary *out) {
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

static gboolean parse_outdoor_walk_v2(MiReader *r, int version, SalusWorkoutSummary *out) {
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
    uint32_t z1 = 0, z2 = 0, z3 = 0, z4 = 0, z5 = 0;
    if (!mi_u32(r, &z1) || !mi_u32(r, &z2) || !mi_u32(r, &z3) || !mi_u32(r, &z4) || !mi_u32(r, &z5)) {
        return FALSE;
    }
    (void)workout_type;
    (void)pace_max;
    (void)pace_min;
    (void)speed_max;
    (void)kcal_total;
    fill_core(out, start, end, active, (int)dist, (int)kcal, (int)hr_avg, (int)hr_max, (int)hr_min);
    out->has_walk_detail = TRUE;
    out->walk_steps = (int)steps;
    out->walk_avg_pace_sec_per_km = (int)pace_avg;
    if (out->walk_avg_pace_sec_per_km <= 0 && dist > 0 && out->duration_sec > 0) {
        out->walk_avg_pace_sec_per_km = (int)((uint64_t)out->duration_sec * 1000u / dist);
    }
    out->walk_avg_speed_kmh = speed_avg;
    out->walk_step_length_cm = (int)step_len;
    out->walk_avg_cadence_spm = (int)step_rate_avg;
    out->walk_max_cadence_spm = (int)step_rate_max;
    out->walk_te_aerobic = te_aerobic;
    out->walk_te_anaerobic = te_anaerobic;
    out->hr_zones.zone1_sec = (int)z1;
    out->hr_zones.zone2_sec = (int)z2;
    out->hr_zones.zone3_sec = (int)z3;
    out->hr_zones.zone4_sec = (int)z4;
    out->hr_zones.zone5_sec = (int)z5;
    return TRUE;
}

static gboolean parse_hiit(MiReader *r, SalusWorkoutSummary *out) {
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

static gboolean parse_elliptical(MiReader *r, int version, SalusWorkoutSummary *out) {
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
                                  SalusWorkoutSummary *out) {
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

gboolean salus_parse_workout_summary(const uint8_t *payload,
                                      size_t payload_len,
                                      int subtype,
                                      int version,
                                      uint32_t file_ts_sec,
                                      SalusWorkoutSummary *out) {
    if (payload == NULL || out == NULL || payload_len < 13) {
        return FALSE;
    }

    salus_workout_summary_clear(out);

    out->sport_subtype = subtype;
    out->version = version;

    /* Mi Fitness's own decoding (field tables / schemas extracted from the app) for every sport. */
    {
        SalusWorkoutSummary trial = {0};
        trial.sport_subtype = subtype;
        trial.version = version;
        if (mi_fitness_parse_sport_report(payload, payload_len, subtype, version, &trial) &&
            summary_values_sane(&trial)) {
            const int shown = trial.sub_sport_type > 0 ? trial.sub_sport_type : subtype;
            trial.sport_name = g_strdup(xiaomi_sport_name(shown));
            *out = trial;
            return TRUE;
        }
        salus_workout_summary_clear(&trial);
    }

    /* Versions Mi Fitness itself cannot read: older Gadgetbridge-style layouts. */
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
        SalusWorkoutSummary trial = {0};
        trial.sport_subtype = subtype;
        trial.version = version;
        trial.sport_name = g_strdup(xiaomi_sport_name(subtype));
        if (!parse_with_header(payload, payload_len, subtype, version, headers[i], &trial)) {
            salus_workout_summary_clear(&trial);
            continue;
        }
        if (summary_values_sane(&trial)) {
            finalize_walk_summary(subtype, &trial);
            *out = trial;
            return TRUE;
        }
        salus_workout_summary_clear(&trial);
    }

    if (subtype == 0x16 && heuristic_walk_summary(payload, payload_len, file_ts_sec, out)) {
        out->sport_subtype = subtype;
        out->version = version;
        out->sport_name = g_strdup(xiaomi_sport_name(subtype));
        finalize_walk_summary(subtype, out);
        return TRUE;
    }

    return FALSE;
}

static void log_walk_summary(SalusApp *state, const SalusWorkoutSummary *sum, int version) {
    if (sum == NULL || !sum->has_walk_detail) {
        return;
    }
    gchar pace[32];
    salus_format_walk_pace(sum->walk_avg_pace_sec_per_km, pace, sizeof(pace));
    gchar *line = g_strdup_printf(
        "Outdoor walk v%d: %d min, %d m, %d kcal, %d steps, pace %s/km, cadence %d, HR %d/%d/%d",
        version,
        sum->duration_sec > 0 ? sum->duration_sec / 60 : 0,
        sum->distance_m > 0 ? sum->distance_m : 0,
        sum->calories > 0 ? sum->calories : 0,
        sum->walk_steps,
        pace,
        sum->walk_avg_cadence_spm,
        sum->hr_avg,
        sum->hr_max,
        sum->hr_min);
    append_log(state, line);
    g_free(line);
}

static void log_swim_summary(SalusApp *state, const SalusWorkoutSummary *sum, int version) {
    if (sum == NULL || !sum->has_swim_detail) {
        return;
    }
    gchar pace[32];
    salus_format_swim_pace(sum->swim_avg_pace_sec_per_100m, pace, sizeof(pace));
    gchar *line = g_strdup_printf(
        "Pool swim v%d: %d kcal, %d lengths, %s, pace %s/100m, SWOLF %d, strokes %d, SPM %d, load %d, recovery %d min, vitality +%d, HR %d/%d/%d",
        version,
        sum->calories,
        sum->swim_lengths,
        salus_swim_stroke_name(sum->swim_main_stroke),
        pace,
        sum->swim_avg_swolf,
        sum->swim_total_strokes,
        sum->swim_avg_spm,
        sum->swim_training_load,
        sum->swim_recovery_min,
        sum->swim_vitality_points,
        sum->hr_avg,
        sum->hr_max,
        sum->hr_min);
    append_log(state, line);
    g_free(line);
}

const char *xiaomi_sport_name(int subtype) {
    /* Mi Fitness FitnessSportType ids: 1–26 are the band's file types, 100+ specific sports. */
    const char *name = mi_fitness_sport_type_name(subtype);
    return name != NULL ? name : "Workout";
}

void salus_apply_workout_file(SalusApp *state,
                               uint32_t file_ts,
                               int subtype,
                               int detail,
                               int version,
                               const uint8_t *payload,
                               size_t payload_len) {
    if (state == NULL || payload == NULL) {
        return;
    }

    salus_schema_maybe_capture(payload, payload_len, subtype, detail, version, file_ts);

    if (detail == 2) {
        SalusWorkoutSummary patch = {0};
        if (salus_parse_workout_gps(payload, payload_len, subtype, version, &patch)) {
            patch.sport_name = g_strdup(xiaomi_sport_name(subtype));
            salus_sport_merge_attachment(state, file_ts, subtype, &patch);
            gchar *line = g_strdup_printf("Workout GPS: %u track point(s)",
                                          patch.gps_track != NULL ? patch.gps_track->len : 0u);
            append_log(state, line);
            g_free(line);
        } else {
            gchar *line = g_strdup_printf("Workout GPS track (%s v%d, %zu bytes — no points decoded)",
                                          mi_fitness_sport_report_parser_name(subtype),
                                          version,
                                          payload_len);
            append_log(state, line);
            g_free(line);
        }
        salus_workout_summary_clear(&patch);
        return;
    }
    if (detail == 0) {
        SalusWorkoutSummary patch = {0};
        const gboolean ok = salus_parse_workout_record(payload, payload_len, subtype, version, &patch);
        if (ok) {
            patch.sport_name = g_strdup(xiaomi_sport_name(subtype));
            salus_sport_merge_attachment(state, file_ts, subtype, &patch);
            gchar *line = g_strdup_printf("Workout record (%s v%d): %u sample(s), interval ~%d s",
                                          mi_fitness_sport_record_parser_name(subtype),
                                          version,
                                          patch.record_samples != NULL ? patch.record_samples->len : 0u,
                                          patch.record_interval_sec);
            append_log(state, line);
            g_free(line);
        } else {
            gchar *line = g_strdup_printf("Workout time-series record (%s v%d, %zu bytes — decode failed)",
                                          mi_fitness_sport_record_parser_name(subtype),
                                          version,
                                          payload_len);
            append_log(state, line);
            g_free(line);
            salus_sport_add_entry(state, file_ts, 1, subtype, NULL, xiaomi_sport_name(subtype));
        }
        salus_workout_summary_clear(&patch);
        return;
    }
    if (detail == 3) {
        append_log(state, "Workout HRR blob received (decode TBD).");
        return;
    }

    SalusWorkoutSummary sum = {0};
    sum.file_ts_sec = file_ts;
    sum.sport_subtype = subtype;
    sum.version = version;
    if (salus_parse_workout_summary(payload, payload_len, subtype, version, file_ts, &sum)) {
        sum.file_ts_sec = file_ts;
        sum.sport_subtype = subtype;
        sum.version = version;
        if (sum.sport_name == NULL) {
            sum.sport_name = g_strdup(xiaomi_sport_name(subtype));
        }
        salus_sport_add_entry(state, file_ts, 1, subtype, &sum, NULL);
        salus_dashboard_schedule_refresh(state);
        if (sum.has_swim_detail) {
            log_swim_summary(state, &sum, version);
        } else if (sum.has_walk_detail) {
            log_walk_summary(state, &sum, version);
        } else {
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
        }
    } else {
        gchar *line = g_strdup_printf("Workout summary: %s v%d — parser not ported yet (%zu bytes)",
                                      xiaomi_sport_name(subtype),
                                      version,
                                      payload_len);
        append_log(state, line);
        g_free(line);
        salus_sport_add_entry(state, file_ts, 1, subtype, NULL, xiaomi_sport_name(subtype));
    }
    salus_workout_summary_clear(&sum);
}
