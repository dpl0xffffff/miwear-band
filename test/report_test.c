/* Workout report decoding against the tables extracted from Mi Fitness. */

#include "miwear/activity/mi_fitness_report_tables.h"
#include "miwear/activity/mi_fitness_sport_report.h"
#include "miwear/activity/mi_fitness_validity.h"
#include "miwear/activity/mi_fitness_data.h"
#include "miwear/activity/schema_reader.h"

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
    MiwearFitnessDataId id = {.data_type = 1, .file_type = 1, .sport_type = (uint8_t)sport, .version = (uint8_t)version};
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
    MiwearWorkoutSummary s = {0};
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
    g_assert_true(miwear_workout_has_metric(&s, 40));
    miwear_workout_summary_clear(&s);
    g_byte_array_unref(b);
}

static void test_outdoor_step_subsport(void) {
    /* Version 6: what the user's band sends for walks; field 65 = sub sport (1 outdoor run). */
    const Val v[] = {{65, 1},   {1, 1793019957}, {3, 1800}, {5, 5000}, {7, 380}, {13, 5100}, {15, 172},
                     {14, 188}, {16, 151},       {17, 182}, {18, 96},  {27, 47}, {109, 5},   {110, 120},
                     {111, 3},  {148, 98},       {34, 300}, {30, 60},  {162, 95}};
    GByteArray *b = encode_report(22, 6, v, G_N_ELEMENTS(v));
    MiwearWorkoutSummary s = {0};
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
    miwear_workout_summary_clear(&s);
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
        MiwearFitnessDataId id = {.data_type = 1, .file_type = 1, .sport_type = (uint8_t)sport, .version = (uint8_t)version};
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
    return g_test_run();
}
