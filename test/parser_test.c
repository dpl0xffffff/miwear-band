/* Decoder tests against synthetic payloads built from the Gadgetbridge layouts. */

#include "miwear/activity/daily_details.h"
#include "miwear/activity/mi_fitness_full_day_sleep.h"
#include "miwear/sleep/mi_sleep_packets.h"

#include <glib.h>
#include <string.h>

/* ---- Daily details ------------------------------------------------------------------------ */

typedef struct {
    GByteArray *b;
} Buf;

static void put8(Buf *w, int v) {
    guint8 c = (guint8)v;
    g_byte_array_append(w->b, &c, 1);
}

static void put16le(Buf *w, int v) {
    put8(w, v & 0xff);
    put8(w, (v >> 8) & 0xff);
}

/* File id (7) + padding (1) + header; records follow; caller appends the 4-byte CRC. */
static Buf dd_begin(const guint8 *header, size_t header_len) {
    Buf w = {g_byte_array_new()};
    for (int i = 0; i < 8; i++) {
        put8(&w, 0);
    }
    g_byte_array_append(w.b, header, (guint)header_len);
    return w;
}

static void dd_end(Buf *w) {
    for (int i = 0; i < 4; i++) {
        put8(w, 0xcc); /* CRC (not checked by the decoder) */
    }
}

/* v4 record with every group present: steps word, calories, unknown, distance, hr, energy,
 * unknown, spo2, stress, [extra byte], light, momentum. */
static void dd_v4_record_ex(Buf *w, int steps, gboolean extra, int type, int cal, int intensity, int sport,
                            int dist_m, int hr, int spo2, int stress) {
    put16le(w, (extra ? 0x4000 : 0) | (steps & 0x3fff));
    put8(w, ((type & 0x03) << 6) | (cal & 0x3f));
    put8(w, ((intensity & 0x07) << 5) | (sport & 0x1f));
    put16le(w, dist_m);
    put8(w, hr);
    put8(w, 0x11);
    put16le(w, 0);
    put8(w, spo2);
    put8(w, stress);
    if (extra) {
        put8(w, 0xee);
    }
    put16le(w, 0x1234);
    put16le(w, 0x5678);
}

static void dd_v4_record(Buf *w, int steps, gboolean extra, int cal, int dist_m, int hr, int spo2, int stress) {
    dd_v4_record_ex(w, steps, extra, 0, cal, 0, 0, dist_m, hr, spo2, stress);
}

/* 11 groups, all present with all fields valid; 12th nibble unused. */
static const guint8 v4_header_all[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xf0};

static void test_daily_details_v4(void) {
    Buf w = dd_begin(v4_header_all, sizeof(v4_header_all));
    dd_v4_record(&w, 87, FALSE, 3, 64, 72, 97, 30);
    dd_v4_record(&w, 120, TRUE, 5, 90, 101, 255 /* spo2 as sent */, 255 /* no stress */);
    dd_v4_record(&w, 0, FALSE, 0, 0, 58, 0, 12);
    dd_end(&w);

    GArray *m = miwear_daily_details_decode(4, w.b->data, w.b->len, NULL);
    g_assert_nonnull(m);
    g_assert_cmpuint(m->len, ==, 3);
    const MiwearDailyDetailsMinute *r = (const MiwearDailyDetailsMinute *)m->data;
    g_assert_cmpint(r[0].steps, ==, 87);
    g_assert_cmpint(r[0].active_calories, ==, 3);
    g_assert_cmpint(r[0].distance_cm, ==, 6400);
    g_assert_cmpint(r[0].heart_rate, ==, 72);
    g_assert_cmpint(r[0].spo2, ==, 97);
    g_assert_cmpint(r[0].stress, ==, 30);
    g_assert_cmpint(r[1].steps, ==, 120); /* the extra-entry bit is not part of the steps */
    g_assert_cmpint(r[1].heart_rate, ==, 101);
    g_assert_cmpint(r[1].stress, ==, -1);
    g_assert_cmpint(r[2].heart_rate, ==, 58); /* still aligned after the extra byte */
    g_assert_cmpint(r[2].stress, ==, 12);
    g_array_unref(m);
    g_byte_array_unref(w.b);
}

/* activeType / activityCalories share a byte, and so do activeStreng / sportType. */
static void test_daily_details_intensity(void) {
    Buf w = dd_begin(v4_header_all, sizeof(v4_header_all));
    dd_v4_record_ex(&w, 110, FALSE, 2 /* walk */, 63, 3 /* moderate */, 0, 80, 95, 98, 20);
    dd_v4_record_ex(&w, 160, FALSE, 3 /* run */, 9, 5 /* intense */, 1 /* outdoor run */, 120, 150, 97, 40);
    dd_v4_record_ex(&w, 0, FALSE, 1 /* still */, 1, 7 /* unknown */, 31, 0, 60, 99, 10);
    dd_end(&w);

    GArray *m = miwear_daily_details_decode(4, w.b->data, w.b->len, NULL);
    g_assert_nonnull(m);
    g_assert_cmpuint(m->len, ==, 3);
    const MiwearDailyDetailsMinute *r = (const MiwearDailyDetailsMinute *)m->data;
    g_assert_cmpint(r[0].active_type, ==, 2);
    g_assert_cmpint(r[0].active_calories, ==, 63);
    g_assert_cmpint(r[0].intensity, ==, 3);
    g_assert_cmpint(r[0].sport_type, ==, 0);
    g_assert_cmpint(r[1].active_type, ==, 3);
    g_assert_cmpint(r[1].active_calories, ==, 9);
    g_assert_cmpint(r[1].intensity, ==, 5);
    g_assert_cmpint(r[1].sport_type, ==, 1);
    g_assert_cmpint(r[1].heart_rate, ==, 150);
    g_assert_cmpint(r[2].active_type, ==, 1);
    g_assert_cmpint(r[2].intensity, ==, 7);
    g_assert_cmpint(r[2].sport_type, ==, 31);
    g_array_unref(m);
    g_byte_array_unref(w.b);
}

/* A header nibble without bit 3 means the group is absent and takes no bytes. */
static void test_daily_details_absent_groups(void) {
    /* v3: groups steps(16) cal(8) unk(8) dist(16) hr(8) energy(8) unk(16) spo2(8) stress(8) →
     * 9 groups, header 5 bytes. Present: steps (all valid), hr, stress; everything else absent. */
    const guint8 header[5] = {0xf0, 0x00, 0xc0, 0x00, 0xc0};
    Buf w = dd_begin(header, sizeof(header));
    for (int i = 0; i < 4; i++) {
        put16le(&w, 10 + i);
        put8(&w, 60 + i);
        put8(&w, 20 + i);
    }
    dd_end(&w);
    GArray *m = miwear_daily_details_decode(3, w.b->data, w.b->len, NULL);
    g_assert_nonnull(m);
    g_assert_cmpuint(m->len, ==, 4);
    const MiwearDailyDetailsMinute *r = (const MiwearDailyDetailsMinute *)m->data;
    for (int i = 0; i < 4; i++) {
        g_assert_cmpint(r[i].steps, ==, 10 + i);
        g_assert_cmpint(r[i].heart_rate, ==, 60 + i);
        g_assert_cmpint(r[i].stress, ==, 20 + i);
        g_assert_cmpint(r[i].distance_cm, ==, -1);
        g_assert_cmpint(r[i].spo2, ==, -1);
    }
    g_array_unref(m);
    g_byte_array_unref(w.b);
}

/*
 * Regression for the 8192-steps rows: a file whose records don't end exactly at the CRC (a
 * trailing partial record here) used to make the parser try other header sizes and keep the one
 * that "found" the most steps, misreading every record. The header size is fixed by the version.
 */
static void test_daily_details_trailing_bytes_keep_layout(void) {
    Buf w = dd_begin(v4_header_all, sizeof(v4_header_all));
    for (int i = 0; i < 30; i++) {
        dd_v4_record(&w, i % 3 == 0 ? 40 : 0, FALSE, 1, i % 3 == 0 ? 30 : 0, 60 + i, 98, 25);
    }
    put8(&w, 0x00);
    put8(&w, 0x20); /* start of a truncated record */
    put8(&w, 0x07);
    dd_end(&w);
    GArray *m = miwear_daily_details_decode(4, w.b->data, w.b->len, NULL);
    g_assert_nonnull(m);
    g_assert_cmpuint(m->len, ==, 30); /* truncated record dropped, nothing re-interpreted */
    const MiwearDailyDetailsMinute *r = (const MiwearDailyDetailsMinute *)m->data;
    for (guint i = 0; i < m->len; i++) {
        g_assert_cmpint(r[i].steps, <, 300);
        g_assert_cmpint(r[i].heart_rate, ==, 60 + (int)i);
    }
    g_array_unref(m);
    g_byte_array_unref(w.b);
}

static void test_daily_details_bad_input(void) {
    const char *err = NULL;
    guint8 tiny[10] = {0};
    g_assert_null(miwear_daily_details_decode(7, tiny, sizeof(tiny), &err));
    g_assert_nonnull(err);
    err = NULL;
    g_assert_null(miwear_daily_details_decode(4, tiny, sizeof(tiny), &err));
    g_assert_nonnull(err);
    g_assert_cmpint(miwear_daily_details_header_size(1), ==, 4);
    g_assert_cmpint(miwear_daily_details_header_size(3), ==, 5);
    g_assert_cmpint(miwear_daily_details_header_size(4), ==, 6);
}

/* ---- Sleep packets ------------------------------------------------------------------------ */

static void put16be(Buf *w, int v) {
    put8(w, (v >> 8) & 0xff);
    put8(w, v & 0xff);
}

static void sleep_packet(Buf *w, guint64 ts, int type, const guint8 *data, int len) {
    const guint8 magic[4] = {0xfb, 0xfa, 0xfc, 0xff}; /* read as 0xfffcfafb little-endian */
    g_byte_array_append(w->b, magic, 4);
    put8(w, 17);
    for (int i = 7; i >= 0; i--) {
        put8(w, (int)((ts >> (8 * i)) & 0xff));
    }
    put8(w, 1);
    put8(w, type);
    put16be(w, len);
    g_byte_array_append(w->b, data, (guint)len);
}

#define T0 1763076180u /* 2026-11-13 23:23:00 UTC */

static void test_sleep_summary_big_endian(void) {
    /* Type 16: idx/wake byte, total, awake, light, REM, deep (big-endian), flags, unk. */
    Buf d = {g_byte_array_new()};
    put8(&d, 0x12);
    put16be(&d, 502);
    put16be(&d, 6);
    put16be(&d, 301);
    put16be(&d, 88);
    put16be(&d, 113);
    put8(&d, 0x14);
    put8(&d, 0);
    Buf w = {g_byte_array_new()};
    sleep_packet(&w, T0, 16, d.b->data, (int)d.b->len);

    MiwearSleepStageRow *rows = NULL;
    size_t n = 0;
    MiwearSleepStageDurations dur = {0};
    g_assert_true(mi_sleep_try_embedded_stage_packets(w.b->data, w.b->len, T0, T0 + 504 * 60, &rows, &n, &dur));
    g_assert_cmpint(dur.total_min, ==, 502);
    g_assert_cmpint(dur.awake_min, ==, 6);
    g_assert_cmpint(dur.light_min, ==, 301);
    g_assert_cmpint(dur.rem_min, ==, 88);
    g_assert_cmpint(dur.deep_min, ==, 113);
    g_free(rows);
    g_byte_array_unref(d.b);
    g_byte_array_unref(w.b);
}

static void test_sleep_stage_codes(void) {
    /* Type 17: big-endian words, stage in the top nibble, minutes until the next change below. */
    static const int raw[] = {1, 2, 3, 0, 4};
    static const int want[] = {3, 2, 4, 5, 0}; /* light, deep, REM, awake, not asleep */
    Buf d = {g_byte_array_new()};
    for (size_t i = 0; i < G_N_ELEMENTS(raw); i++) {
        put16be(&d, (raw[i] << 12) | 10);
    }
    Buf w = {g_byte_array_new()};
    sleep_packet(&w, T0, 17, d.b->data, (int)d.b->len);
    /* A second stage packet whose timestamp is unreadable must be skipped, not appended. */
    sleep_packet(&w, 7, 17, d.b->data, (int)d.b->len);

    MiwearSleepStageRow *rows = NULL;
    size_t n = 0;
    MiwearSleepStageDurations dur = {0};
    g_assert_true(mi_sleep_try_embedded_stage_packets(w.b->data, w.b->len, T0, T0 + 3600, &rows, &n, &dur));
    g_assert_cmpuint(n, ==, G_N_ELEMENTS(raw));
    for (size_t i = 0; i < n; i++) {
        g_assert_cmpint(rows[i].stage, ==, want[i]);
        g_assert_cmpint(rows[i].timestamp_ms, ==, ((gint64)T0 + (gint64)i * 600) * 1000);
    }
    g_assert_cmpint(miwear_normalize_sleep_stage(0), ==, -1); /* not asleep is not painted */
    g_assert_cmpint(miwear_normalize_sleep_stage(4), ==, 4);
    g_assert_cmpint(miwear_normalize_sleep_stage(1), ==, 1);
    g_free(rows);
    g_byte_array_unref(d.b);
    g_byte_array_unref(w.b);
}

static void put32le(Buf *w, uint32_t v) {
    put16le(w, (int)(v & 0xffff));
    put16le(w, (int)(v >> 16));
}

/* A v6 full_day_sleep report: HRV summary fields and an HRV series sit between the times and
 * the HR/SpO2 series (validity bits 8-18 and 21), then snore and the featureData tail. */
static void test_full_day_sleep_v6_layout(void) {
    Buf w = {g_byte_array_new()};
    put32le(&w, T0);
    put8(&w, 4);           /* timezone */
    put8(&w, 6);           /* version */
    put8(&w, 8 << 2 | 1);  /* full_day_sleep report */
    put8(&w, 0);           /* no encryption/compression */
    put8(&w, 0xFF);        /* fields 0-7 */
    put8(&w, 0xFF);        /* HRV summary 8-15 */
    put8(&w, 0xE0 | 0x10 | 0x08 | 0x04 | 0x02); /* HRV 16-18, HR, SpO2, HRV series, snore */
    put8(&w, 1);           /* sleepFull */
    put32le(&w, T0);       /* startTime */
    put32le(&w, T0 + 480 * 60);
    put8(&w, 90);
    put8(&w, 95);
    put32le(&w, 10);
    put32le(&w, 490);
    put32le(&w, T0 - 600); /* goBedTime */
    put32le(&w, T0 + 481 * 60);
    for (int i = 0; i < 11; i++) {
        if (i == 6) {
            put32le(&w, T0); /* hrvTimestamp */
        } else {
            put16le(&w, 40 + i);
        }
    }
    put16le(&w, 60); put16le(&w, 3); put32le(&w, T0); put8(&w, 55); put8(&w, 56); put8(&w, 57);
    put16le(&w, 60); put16le(&w, 2); put32le(&w, T0); put8(&w, 97); put8(&w, 98);
    put16le(&w, 300); put16le(&w, 2); put32le(&w, T0); put16le(&w, 41); put16le(&w, 42);
    put16le(&w, 60); put16le(&w, 0); /* snore: no points, so no firstTime */
    const guint feature_at = w.b->len;
    Buf d = {g_byte_array_new()};
    put8(&d, 0x12);
    put16be(&d, 470); put16be(&d, 10); put16be(&d, 366); put16be(&d, 0); put16be(&d, 104);
    put8(&d, 0); put8(&d, 0);
    sleep_packet(&w, T0, 16, d.b->data, (int)d.b->len);
    put32le(&w, 0); /* CRC (not checked here) */

    MiwearFullDaySleepLayout l;
    g_assert_true(miwear_full_day_sleep_layout(w.b->data, w.b->len, 6, &l));
    g_assert_cmpuint(l.go_bed_time, ==, T0 - 600);
    g_assert_cmpuint(l.live_bed_time, ==, T0 + 481 * 60);
    g_assert_cmpuint(l.heart_rate.count, ==, 3);
    g_assert_cmpuint(w.b->data[l.heart_rate.values_off], ==, 55);
    g_assert_cmpuint(l.spo2.count, ==, 2);
    g_assert_cmpuint(w.b->data[l.spo2.values_off + 1], ==, 98);
    g_assert_cmpuint(l.hrv.count, ==, 2);
    g_assert_true(l.snore.present);
    g_assert_cmpuint(l.snore.count, ==, 0);
    g_assert_cmpuint(l.feature_off, ==, feature_at);

    const uint8_t *feature = NULL;
    size_t feature_len = 0;
    g_assert_true(mi_fitness_full_day_sleep_feature_tail(w.b->data, w.b->len, 6, &feature, &feature_len));
    MiwearSleepStageRow *rows = NULL;
    size_t n = 0;
    MiwearSleepStageDurations dur = {0};
    g_assert_true(mi_sleep_try_embedded_stage_packets(feature, feature_len, T0, T0 + 480 * 60, &rows, &n, &dur));
    g_assert_cmpint(dur.deep_min, ==, 104);
    g_assert_cmpint(dur.light_min, ==, 366);
    g_free(rows);

    /* The same bytes read as v5 (HR at bit 8) must not reach the featureData. */
    g_assert_cmpuint(miwear_full_day_sleep_structured_end(w.b->data, w.b->len, 5), !=, feature_at);
    g_byte_array_unref(d.b);
    g_byte_array_unref(w.b);
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/daily-details/v4", test_daily_details_v4);
    g_test_add_func("/daily-details/intensity", test_daily_details_intensity);
    g_test_add_func("/daily-details/absent-groups", test_daily_details_absent_groups);
    g_test_add_func("/daily-details/trailing-bytes", test_daily_details_trailing_bytes_keep_layout);
    g_test_add_func("/daily-details/bad-input", test_daily_details_bad_input);
    g_test_add_func("/sleep/summary-big-endian", test_sleep_summary_big_endian);
    g_test_add_func("/sleep/stage-codes", test_sleep_stage_codes);
    g_test_add_func("/sleep/full-day-v6-layout", test_full_day_sleep_v6_layout);
    return g_test_run();
}
