/* Workout report decoding against the tables extracted from Mi Fitness. */

#include "salus/activity/mi_fitness_report_tables.h"
#include "salus/activity/mi_fitness_sport_report.h"
#include "salus/activity/mi_fitness_validity.h"
#include "salus/activity/mi_fitness_data.h"
#include "salus/activity/schema_reader.h"
#include "salus/activity/mi_fitness_sport_record.h"
#include "salus/activity/workout_record.h"
#include "salus/db/workout_store.h"

#include <math.h>

#include <glib.h>
#include <string.h>

typedef struct {
    int id;
    double value;
} Val;

/* Builds a report file for `sport`/`version` from the native table: every field the version
 * supports is written (value from `vals`, else 0) and marked valid when given. */
static GByteArray *encode_report(int sport, int version, const Val *vals, size_t nvals) {
    size_t count = 0;
    const MiFitnessOneDimenField *f = mi_fitness_report_fields(sport, &count, NULL);
    g_assert_nonnull(f);
    SalusFitnessDataId id = {.data_type = 1, .file_type = 1, .sport_type = (uint8_t)sport, .version = (uint8_t)version};
    const int vlen = mi_fitness_data_validity_len(&id);
    g_assert_cmpint(vlen, >=, 0);

    GByteArray *b = g_byte_array_new();
    const guint8 head[8] = {0x10, 0x20, 0x30, 0x60, 0x04, (guint8)version, (guint8)(0x80 | (sport << 2) | 1), 0};
    g_byte_array_append(b, head, 8);
    const guint valid_at = b->len;
    for (int i = 0; i < vlen; i++) {
        const guint8 z = 0;
        g_byte_array_append(b, &z, 1);
    }
    int bit = 0;
    for (size_t i = 0; i < count; i++) {
        if (f[i].support_version > version || f[i].has_depend) {
            continue; /* dependent extras (course / lactate) are absent without their trigger */
        }
        double v = 0;
        gboolean given = FALSE;
        for (size_t k = 0; k < nvals; k++) {
            if (vals[k].id == f[i].type) {
                v = vals[k].value;
                given = TRUE;
            }
        }
        if (f[i].type >= 0) {
            if (given) {
                b->data[valid_at + bit / 8] |= (guint8)(1u << (7 - bit % 8));
            }
            bit++;
        }
        guint8 raw[8] = {0};
        if (f[i].is_float) {
            const float fv = (float)v;
            memcpy(raw, &fv, 4);
        } else {
            const gint64 iv = (gint64)v;
            for (int k = 0; k < f[i].byte_count && k < 8; k++) {
                raw[k] = (guint8)(iv >> (8 * k));
            }
        }
        g_byte_array_append(b, raw, f[i].byte_count);
    }
    const guint8 crc[4] = {0};
    g_byte_array_append(b, crc, 4);
    return b;
}

static void test_swim_report(void) {
    const Val v[] = {{1, 1700000000}, {2, 1700002400}, {3, 2400}, {5, 1500}, {7, 410}, {16, 132}, {17, 168},
                     {18, 90},        {35, 920},       {36, 2},   {39, 60},  {40, 38}, {42, 25}, {157, 31},
                     {110, 88},       {111, 2},        {29, 20},  {117, 12}};
    GByteArray *b = encode_report(9, 8, v, G_N_ELEMENTS(v)); /* v8: heart rate exists from v8 on */
    SalusWorkoutSummary s = {0};
    g_assert_true(mi_fitness_parse_sport_report(b->data, b->len, 9, 8, &s));
    g_assert_cmpint(s.duration_sec, ==, 2400);
    g_assert_cmpint(s.distance_m, ==, 1500);
    g_assert_cmpint(s.calories, ==, 410);
    g_assert_cmpint(s.hr_avg, ==, 132);
    g_assert_cmpint(s.hr_max, ==, 168); /* > 127: fields are unsigned */
    g_assert_cmpint(s.swim_total_strokes, ==, 920);
    g_assert_cmpint(s.swim_main_stroke, ==, 2);
    g_assert_cmpint(s.swim_lengths, ==, 60);
    g_assert_cmpint(s.swim_avg_swolf, ==, 38);
    g_assert_cmpint(s.swim_pool_width_m, ==, 25);
    g_assert_cmpint(s.training_load, ==, 88);
    g_assert_cmpint(s.training_load_level, ==, 2);
    g_assert_cmpint(s.recovery_min, ==, 20 * 60);
    g_assert_true(s.has_swim_detail);
    g_assert_true(salus_workout_has_metric(&s, 40));
    salus_workout_summary_clear(&s);
    g_byte_array_unref(b);
}

static void test_outdoor_step_subsport(void) {
    /* Version 6: what the user's band sends for walks; field 65 = sub sport (1 outdoor run). */
    const Val v[] = {{65, 1},   {1, 1793019957}, {3, 1800}, {5, 5000}, {7, 380}, {13, 5100}, {15, 172},
                     {14, 188}, {16, 151},       {17, 182}, {18, 96},  {27, 47}, {109, 5},   {110, 120},
                     {111, 3},  {148, 98},       {34, 300}, {30, 60},  {162, 95}};
    GByteArray *b = encode_report(22, 6, v, G_N_ELEMENTS(v));
    SalusWorkoutSummary s = {0};
    g_assert_true(mi_fitness_parse_sport_report(b->data, b->len, 22, 6, &s));
    g_assert_cmpint(s.sub_sport_type, ==, 1);
    g_assert_cmpstr(mi_fitness_sport_type_name(s.sub_sport_type), ==, "Outdoor run");
    g_assert_cmpint(s.duration_sec, ==, 1800);
    g_assert_cmpint(s.walk_steps, ==, 5100);
    g_assert_cmpint(s.walk_avg_cadence_spm, ==, 172); /* 15 = avg, 14 = max */
    g_assert_cmpint(s.walk_max_cadence_spm, ==, 188);
    g_assert_cmpint(s.hr_avg, ==, 151);
    g_assert_cmpint(s.hr_max, ==, 182);
    g_assert_cmpint(s.vo2_max, ==, 47);
    g_assert_cmpint(s.vo2_max_level, ==, 5);
    g_assert_cmpint(s.training_load, ==, 120);
    g_assert_cmpint(s.walk_step_length_cm, ==, 98);
    g_assert_cmpint(s.hr_zones.zone1_sec, ==, 300); /* 34 = zone 1 (warm-up) */
    g_assert_cmpint(s.hr_zones.zone5_sec, ==, 60);
    g_assert_cmpint(s.hr_zones.zone1_lower_bpm, ==, 0); /* zone limits only exist from v7 */
    salus_workout_summary_clear(&s);
    g_byte_array_unref(b);
}

/* Every sport/version Mi Fitness has a schema for: decoding through the schema-derived table must
 * give exactly what the native table gives (the two paths Mi Fitness itself uses). */
static void test_schema_matches_native(void) {
    static const struct {
        int sport, version;
    } cases[] = {
        {3, 13}, {7, 10}, {8, 11}, {8, 12}, {8, 13}, {8, 14}, {9, 8}, {10, 8}, {11, 7}, {12, 7}, {13, 8},
        {14, 7}, {16, 7}, {18, 5}, {19, 5}, {20, 4}, {21, 6}, {22, 8}, {22, 9}, {23, 8}, {24, 2},
    };
    GRand *rnd = g_rand_new_with_seed(42);
    for (size_t c = 0; c < G_N_ELEMENTS(cases); c++) {
        const int sport = cases[c].sport, version = cases[c].version;
        SalusFitnessDataId id = {.data_type = 1, .file_type = 1, .sport_type = (uint8_t)sport, .version = (uint8_t)version};
        const int vlen = mi_fitness_data_validity_len(&id);
        size_t count = 0;
        const MiFitnessOneDimenField *f = mi_fitness_report_fields(sport, &count, NULL);
        size_t body = 0;
        for (size_t i = 0; i < count; i++) {
            if (f[i].support_version <= version) {
                body += f[i].byte_count;
            }
        }
        for (int round = 0; round < 50; round++) {
            GByteArray *b = g_byte_array_sized_new(8 + vlen + body + 4);
            const guint8 head[8] = {1, 2, 3, 4, 4, (guint8)version, (guint8)(0x80 | (sport << 2) | 1), 0};
            g_byte_array_append(b, head, 8);
            for (size_t i = 0; i < (size_t)vlen + body + 4; i++) {
                guint8 r = (guint8)g_rand_int_range(rnd, 0, 256);
                g_byte_array_append(b, &r, 1);
            }
            /* selectedCourse (90): Mi Fitness's native table adds the course id for 251 too, its schema
             * only for 252/253/255 — keep clear of that one disagreement between the app's own paths. */
            size_t off = 8 + (size_t)vlen;
            for (size_t i = 0; i < count; i++) {
                if (f[i].support_version > version) {
                    continue;
                }
                if (f[i].type == 90 && b->data[off] == 251) {
                    b->data[off] = 0;
                }
                if (f[i].has_depend) {
                    break; /* stop the walk: offsets past a dependent field depend on the value */
                }
                off += f[i].byte_count;
            }

            MiFitnessFieldValue schema_vals[MI_FITNESS_FIELD_VALUE_MAX], native_vals[MI_FITNESS_FIELD_VALUE_MAX];
            size_t ns = 0, nn = 0;
            const char *src = NULL;
            g_assert_true(mi_fitness_sport_report_decode(b->data, b->len, sport, version, schema_vals, &ns, &src));
            g_assert_cmpstr(src, ==, "schema");
            g_assert_true(mi_fitness_oneden_parse_report(f, count, version, b->data + 8, (size_t)vlen,
                                                         b->data + 8 + vlen, b->len - 8 - vlen - 4, native_vals, &nn));
            /* Compare the values both paths read for ids >= 0 (schema-only extras have none). */
            for (size_t i = 0; i < nn; i++) {
                if (native_vals[i].type < 0) {
                    continue;
                }
                gboolean found = FALSE;
                for (size_t k = 0; k < ns; k++) {
                    if (schema_vals[k].type == native_vals[i].type) {
                        found = TRUE;
                        if (schema_vals[k].valid != native_vals[i].valid ||
                            schema_vals[k].i64 != native_vals[i].i64 ||
                            memcmp(&schema_vals[k].f32, &native_vals[i].f32, sizeof(float)) != 0) {
                            g_error("sport %d v%d: field %d differs (schema %d/%" G_GINT64_FORMAT
                                    ", native %d/%" G_GINT64_FORMAT ")",
                                    sport, version, native_vals[i].type, schema_vals[k].valid, schema_vals[k].i64,
                                    native_vals[i].valid, native_vals[i].i64);
                        }
                    }
                }
                if (!found) {
                    g_error("sport %d v%d: field %d missing on the schema path", sport, version, native_vals[i].type);
                }
            }
            g_byte_array_unref(b);
        }
    }
    g_rand_free(rnd);
}

/* ---- Records (file type 0) --------------------------------------------------------------- */

/* Wraps a record body: header with `valid` (padded to the version's validity length) + CRC. */
static GByteArray *record_file(int sport, int version, const guint8 *valid, size_t nvalid, GByteArray *body) {
    SalusFitnessDataId id = {.data_type = 1, .file_type = 0, .sport_type = (uint8_t)sport, .version = (uint8_t)version};
    const int vlen = mi_fitness_data_validity_len(&id);
    g_assert_cmpint(vlen, >=, (int)nvalid);
    GByteArray *b = g_byte_array_new();
    const guint8 head[8] = {1, 2, 3, 4, 0, (guint8)version, (guint8)(0x80 | (sport << 2)), 0};
    g_byte_array_append(b, head, 8);
    for (int i = 0; i < vlen; i++) {
        const guint8 v = (size_t)i < nvalid ? valid[i] : 0;
        g_byte_array_append(b, &v, 1);
    }
    g_byte_array_append(b, body->data, body->len);
    const guint8 crc[4] = {0};
    g_byte_array_append(b, crc, 4);
    g_byte_array_unref(body);
    return b;
}

static void put_u8(GByteArray *b, unsigned v) {
    const guint8 x = (guint8)v;
    g_byte_array_append(b, &x, 1);
}
static void put_u16(GByteArray *b, unsigned v) {
    put_u8(b, v & 0xFF);
    put_u8(b, (v >> 8) & 0xFF);
}
static void put_u32(GByteArray *b, guint32 v) {
    put_u16(b, v & 0xFFFF);
    put_u16(b, v >> 16);
}
static void put_f32(GByteArray *b, float f) {
    guint32 v;
    memcpy(&v, &f, 4);
    put_u32(b, v);
}

static gboolean parse_record(GByteArray *file, int sport, int version, SalusWorkoutSummary *s) {
    const gboolean ok = salus_parse_workout_record(file->data, file->len, sport, version, s);
    g_byte_array_unref(file);
    return ok;
}

#define SAMPLE(s, i) (&g_array_index((s)->record_samples, SalusWorkoutSample, (i)))

/* Outdoor walk v6: FourDimen items, start altitude in the init data, a pause between segments. */
static void step_record(GByteArray *b, unsigned cal, unsigned steps, unsigned hr, unsigned up, unsigned dm) {
    put_u8(b, (cal << 4) | steps);    /* [2 calories | 4 steps]                  */
    put_u8(b, hr);                    /* 5 heart rate                             */
    put_u8(b, (up << 6) | dm);        /* [6 whole km | 7 climbing | 8 change dm]  */
    put_u8(b, 12);                    /* 9 distance (dm)                          */
    put_u8(b, 150);                   /* 49 cadence                               */
    put_u16(b, 400);                  /* 12 pace (s/km)                           */
    put_u16(b, 210);                  /* 57 running power                         */
}

static void step_segment(GByteArray *b, float height, guint32 count, guint32 start) {
    put_f32(b, height);
    put_u32(b, count);
    put_u32(b, start);
    for (int i = 0; i < 11; i++) {
        put_u8(b, 0); /* interval-training summary v6: 41, 43, 54, 55 */
    }
}

static void test_record_outdoor_step(void) {
    /* Nibbles for the v6 items in order (2/4, 5, 6/7/8, 9, 40, 44-46, 48, 49, 12, 56, 57):
     * 8 = item present, 4/2/1 = high/middle/low valid. Stride, running form and 56 are absent. */
    const guint8 valid[] = {0xEC, 0xFC, 0x00, 0x0C, 0xC0, 0xC0};
    GByteArray *body = g_byte_array_new();
    step_segment(body, 100.0f, 3, 1000);
    step_record(body, 1, 2, 120, 1, 5);
    step_record(body, 0, 3, 125, 0, 10);
    step_record(body, 2, 1, 130, 1, 0);
    step_segment(body, 50.0f, 2, 1010); /* resumed after a 7 s pause */
    step_record(body, 1, 2, 140, 1, 3);
    step_record(body, 1, 2, 141, 1, 3);

    SalusWorkoutSummary s = {0};
    g_assert_true(parse_record(record_file(22, 6, valid, sizeof(valid), body), 22, 6, &s));
    g_assert_cmpuint(s.record_samples->len, ==, 5);
    const guint32 off[] = {0, 1, 2, 10, 11};
    const guint8 hr[] = {120, 125, 130, 140, 141};
    const float alt[] = {100.5f, 99.5f, 99.5f, 50.3f, 50.6f};
    for (guint i = 0; i < 5; i++) {
        g_assert_cmpuint(SAMPLE(&s, i)->offset_sec, ==, off[i]);
        g_assert_cmpuint(SAMPLE(&s, i)->hr, ==, hr[i]);
        g_assert_cmpfloat_with_epsilon(SAMPLE(&s, i)->v[SALUS_SERIES_ALTITUDE], alt[i], 1e-3);
        g_assert_cmpfloat_with_epsilon(SAMPLE(&s, i)->v[SALUS_SERIES_DISTANCE], 1.2, 1e-6);
        g_assert_cmpfloat(SAMPLE(&s, i)->v[SALUS_SERIES_CADENCE], ==, 150);
        g_assert_cmpfloat(SAMPLE(&s, i)->v[SALUS_SERIES_PACE], ==, 400);
        g_assert_cmpfloat(SAMPLE(&s, i)->v[SALUS_SERIES_POWER], ==, 210);
        g_assert_true(isnan(SAMPLE(&s, i)->v[SALUS_SERIES_STRIDE])); /* item absent */
    }
    g_assert_cmpuint(SAMPLE(&s, 1)->steps_delta, ==, 3);
    g_assert_cmpfloat(SAMPLE(&s, 2)->v[SALUS_SERIES_CALORIES], ==, 2);
    g_assert_cmpint(s.record_interval_sec, ==, 2); /* 11 s over 4 gaps */

    /* The series survive the DB JSON round trip; older rows without extra columns still load. */
    gchar *json = salus_workout_summary_to_json(22, &s);
    SalusWorkoutSummary back = {0};
    int sport = 0;
    g_assert_true(salus_workout_summary_from_json(json, &sport, &back));
    g_assert_cmpuint(back.record_samples->len, ==, 5);
    for (guint i = 0; i < 5; i++) {
        for (int k = 0; k < SALUS_SERIES_COUNT; k++) {
            const float a = SAMPLE(&s, i)->v[k], c = SAMPLE(&back, i)->v[k];
            g_assert_true(isnan(a) ? isnan(c) : fabsf(a - c) < 0.01f);
        }
        g_assert_cmpuint(SAMPLE(&back, i)->hr, ==, hr[i]);
    }
    g_free(json);
    salus_workout_summary_clear(&back);
    salus_workout_summary_clear(&s);
}

/* Pool swim v3: OneDimen lap records carrying their own end time. */
static void swim_lap(GByteArray *b, guint32 end, unsigned swolf, unsigned spm) {
    put_u8(b, 0);        /* -1 record type: 0 = lap (the fields below depend on it) */
    put_u32(b, end);     /* 1 end time                                              */
    put_u8(b, 2);        /* 11 stroke                                               */
    put_u16(b, 120);     /* 12 pace                                                 */
    put_u16(b, swolf);   /* 13 SWOLF                                                */
    put_u16(b, 25);      /* 9 distance (m, distance_times 1)                        */
    put_u16(b, 9);       /* 2 calories                                              */
    put_u16(b, 18);      /* 16 strokes                                              */
    put_u16(b, 1);       /* 10 count                                                */
    put_u8(b, spm);      /* 17 stroke rate                                          */
    for (int i = 0; i < 5; i++) {
        put_u8(b, 0);    /* 18-22 per-stroke rates                                  */
    }
    put_u16(b, 0);       /* 3 total calories                                        */
    put_u8(b, 0);        /* 81-83 breath / pitch angles                             */
    put_u8(b, 0);
    put_u8(b, 0);
    put_u16(b, 0);       /* 84                                                      */
    put_u32(b, 2);       /* 85 heart-rate sample count …                            */
    put_u8(b, 130);      /* 86 … and the samples                                    */
    put_u8(b, 131);
}

static void test_record_swim(void) {
    const guint8 valid[] = {0xFF, 0xFF, 0xFF};
    GByteArray *body = g_byte_array_new();
    put_u32(body, 2);
    put_u32(body, 5000);
    swim_lap(body, 5060, 45, 30);
    swim_lap(body, 5125, 48, 28);
    SalusWorkoutSummary s = {0};
    g_assert_true(parse_record(record_file(9, 3, valid, sizeof(valid), body), 9, 3, &s));
    g_assert_cmpuint(s.record_samples->len, ==, 2);
    g_assert_cmpuint(SAMPLE(&s, 0)->offset_sec, ==, 60); /* laps sit at their end */
    g_assert_cmpuint(SAMPLE(&s, 1)->offset_sec, ==, 125);
    g_assert_cmpfloat(SAMPLE(&s, 1)->v[SALUS_SERIES_SWOLF], ==, 48);
    g_assert_cmpfloat(SAMPLE(&s, 0)->v[SALUS_SERIES_STROKE_RATE], ==, 30);
    g_assert_cmpfloat(SAMPLE(&s, 0)->v[SALUS_SERIES_DISTANCE], ==, 25);
    g_assert_cmpfloat(SAMPLE(&s, 0)->v[SALUS_SERIES_PACE], ==, 120);
    salus_workout_summary_clear(&s);
}

/* Rope skipping v2: GroupSportRecordParser framing (sets + rest records). */
static void test_record_group(void) {
    const guint8 valid[] = {0xF8}; /* 5 hr, 2 calories, 35 count, 36 jumps, 37 stumbles */
    GByteArray *body = g_byte_array_new();
    put_u32(body, 3);
    put_u32(body, 2000);
    put_u8(body, 1);    /* last set of a group … */
    put_u32(body, 5);   /* … of 5 records: 2 rest records follow */
    for (int i = 0; i < 5; i++) {
        const guint8 r[5] = {(guint8)(150 + i), 1, 0, 0, 0};
        g_byte_array_append(body, r, 5);
    }
    put_u32(body, 2);
    put_u32(body, 2010);
    put_u8(body, 0);
    put_u32(body, 0);
    for (int i = 0; i < 2; i++) {
        const guint8 r[5] = {(guint8)(160 + i), 1, 0, 0, 0};
        g_byte_array_append(body, r, 5);
    }
    GArray *recs = g_array_new(FALSE, FALSE, sizeof(MiFitnessRecord));
    GArray *segs = g_array_new(FALSE, FALSE, sizeof(MiFitnessRecordSegment));
    GByteArray *file = record_file(14, 2, valid, sizeof(valid), body);
    const char *parser = NULL;
    g_assert_true(mi_fitness_sport_record_decode(file->data, file->len, 14, 2, recs, segs, &parser));
    g_assert_cmpstr(parser, ==, "RopeSkippingRecordParser");
    g_assert_cmpuint(segs->len, ==, 3); /* set, rest, set */
    g_assert_cmpuint(recs->len, ==, 7);
    const guint32 starts[] = {2000, 2001, 2002, 2003, 2004, 2010, 2011};
    for (guint i = 0; i < 7; i++) {
        const MiFitnessRecord *r = &g_array_index(recs, MiFitnessRecord, i);
        int64_t v = 0;
        g_assert_cmpuint(r->start_sec, ==, starts[i]);
        g_assert_true(mi_fitness_record_get(r, 5, &v));
        g_assert_cmpint(v, ==, i < 5 ? 150 + i : 160 + i - 5);
    }
    g_byte_array_unref(file);
    g_array_unref(recs);
    g_array_unref(segs);
}

/* Diving v1: signed water temperature and vertical speed whose sign comes from item 73. */
static void dive_record(GByteArray *b, unsigned depth_dm, int temp_dc, unsigned dir, unsigned speed) {
    put_u8(b, 1);                              /* 70 status                         */
    put_u16(b, depth_dm);                      /* 71 depth (dm)                     */
    put_u16(b, (guint16)(gint16)temp_dc);      /* 72 water temperature (0.1 °C)     */
    put_u16(b, (dir << 14) | speed);           /* [73 direction (2 bits) | 74 speed] */
}

static void test_record_diving(void) {
    const guint8 valid[] = {0xCC, 0xCE}; /* 70, 71, 72 present; 73/74 present with both values */
    GByteArray *body = g_byte_array_new();
    put_u32(body, 3);
    put_u32(body, 3000);
    put_u8(body, 0); /* summary: 64 = 0, so the dive-section fields that depend on it are absent */
    dive_record(body, 105, -15, 1, 12);
    dive_record(body, 110, 210, 0, 8);
    dive_record(body, 112, 205, 2, 5); /* neither up nor down: Mi Fitness reports 0 */
    SalusWorkoutSummary s = {0};
    g_assert_true(parse_record(record_file(25, 1, valid, sizeof(valid), body), 25, 1, &s));
    g_assert_cmpuint(s.record_samples->len, ==, 3);
    g_assert_cmpfloat_with_epsilon(SAMPLE(&s, 0)->v[SALUS_SERIES_DEPTH], 10.5, 1e-5);
    g_assert_cmpfloat_with_epsilon(SAMPLE(&s, 0)->v[SALUS_SERIES_WATER_TEMP], -1.5, 1e-5);
    g_assert_cmpfloat_with_epsilon(SAMPLE(&s, 0)->v[SALUS_SERIES_SPEED], 1.2, 1e-5);
    g_assert_cmpfloat_with_epsilon(SAMPLE(&s, 1)->v[SALUS_SERIES_SPEED], -0.8, 1e-5);
    g_assert_cmpfloat(SAMPLE(&s, 2)->v[SALUS_SERIES_SPEED], ==, 0.0);
    salus_workout_summary_clear(&s);
}

static void test_record_unsupported(void) {
    GByteArray *body = g_byte_array_new();
    put_u32(body, 0);
    put_u32(body, 0);
    SalusWorkoutSummary s = {0};
    const guint8 valid[] = {0};
    /* Triathlon has no record parser in Mi Fitness; a version past the tables has no layout. */
    GByteArray *f = record_file(22, 6, valid, 1, body);
    g_assert_false(salus_parse_workout_record(f->data, f->len, 17, 1, &s));
    g_assert_false(salus_parse_workout_record(f->data, f->len, 22, 99, &s));
    g_byte_array_unref(f);
}

static void test_names(void) {
    g_assert_cmpstr(mi_fitness_sport_type_name(9), ==, "Pool swim");
    g_assert_cmpstr(mi_fitness_sport_type_name(13), ==, "Rowing machine");
    g_assert_cmpstr(mi_fitness_sport_type_name(608), ==, "Badminton");
    g_assert_null(mi_fitness_sport_type_name(99999));
    g_assert_cmpint(mi_fitness_schema_field_id("avgHeartRate"), ==, 16);
    g_assert_cmpint(mi_fitness_schema_field_id("trainingLoad"), ==, 110);
    g_assert_cmpint(mi_fitness_schema_field_id("noSuchField"), ==, MI_FITNESS_FIELD_UNKNOWN);
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/report/swim", test_swim_report);
    g_test_add_func("/report/outdoor-step-subsport", test_outdoor_step_subsport);
    g_test_add_func("/report/schema-matches-native", test_schema_matches_native);
    g_test_add_func("/report/names", test_names);
    g_test_add_func("/record/outdoor-step", test_record_outdoor_step);
    g_test_add_func("/record/swim-laps", test_record_swim);
    g_test_add_func("/record/group", test_record_group);
    g_test_add_func("/record/diving", test_record_diving);
    g_test_add_func("/record/unsupported", test_record_unsupported);
    return g_test_run();
}
