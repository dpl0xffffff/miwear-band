/* Decoder tests against synthetic payloads built from the Gadgetbridge layouts. */

#include "miwear/activity/daily_details.h"
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
static void dd_v4_record(Buf *w, int steps, gboolean extra, int cal, int dist_m, int hr, int spo2, int stress) {
    put16le(w, (extra ? 0x4000 : 0) | (steps & 0x3fff));
    put8(w, cal & 0x3f);
    put8(w, 0x00);
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

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/daily-details/v4", test_daily_details_v4);
    g_test_add_func("/daily-details/absent-groups", test_daily_details_absent_groups);
    g_test_add_func("/daily-details/trailing-bytes", test_daily_details_trailing_bytes_keep_layout);
    g_test_add_func("/daily-details/bad-input", test_daily_details_bad_input);
    g_test_add_func("/sleep/summary-big-endian", test_sleep_summary_big_endian);
    g_test_add_func("/sleep/stage-codes", test_sleep_stage_codes);
    return g_test_run();
}
