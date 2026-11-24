#include "miwear/activity/mi_fitness_sport_record.h"

#include "miwear/activity/mi_fitness_report_tables.h"
#include "miwear/activity/mi_fitness_validity.h"
#include "miwear/util.h"

#include <math.h>
#include <string.h>

/* ---- FourDimenDataParser ------------------------------------------------------------------- */

typedef struct {
    bool exist, high, middle, low;
} FourValid;

/* parseDataValid: each item the version supports takes the next nibble (high nibble first):
 * 8 = item present, 4/2/1 = high/middle/low value valid. Unsupported items are absent. */
static void four_valid(const MiFitnessRecordLayout *l, int version, const uint8_t *bits, size_t bits_len, FourValid *out) {
    size_t nib = 0;
    for (size_t i = 0; i < l->count; i++) {
        const MiFitnessFourDimenField *f = &l->four[i];
        memset(&out[i], 0, sizeof(out[i]));
        if (f->max_support_version >= 0 && f->max_support_version < version) {
            continue;
        }
        if (f->support_version > version || nib / 2 >= bits_len) {
            continue;
        }
        const uint8_t b = bits[nib / 2];
        const unsigned n = nib % 2 == 0 ? (b >> 4) & 0xF : b & 0xF;
        out[i] = (FourValid){(n & 8) != 0, (n & 4) != 0, (n & 2) != 0, (n & 1) != 0};
        nib++;
    }
}

static size_t four_record_len(const MiFitnessRecordLayout *l, const FourValid *valid) {
    size_t len = 0;
    for (size_t i = 0; i < l->count; i++) {
        if (valid[i].exist) {
            len += l->four[i].byte_size;
        }
    }
    return len;
}

/* getByteData: little-endian 1/2/4 bytes; a signed item is sign-extended, as Java's int is. */
static int32_t read_item(const uint8_t *p, int size, bool is_unsigned) {
    switch (size) {
        case 1:
            return is_unsigned ? (int32_t)p[0] : (int32_t)(int8_t)p[0];
        case 2: {
            const uint16_t v = miwear_read_u16_le(p);
            return is_unsigned ? (int32_t)v : (int32_t)(int16_t)v;
        }
        default:
            return (int32_t)miwear_read_u32_le(p);
    }
}

/* computeItemValue: the whole value, or bits [start, start + size) (logical shift, as `>>>`). */
static int64_t item_value(const MiFitnessItemLen *len, int32_t data, int byte_size) {
    if (len->byte_size == byte_size) {
        return data;
    }
    const uint32_t mask = len->bit_size >= 32 ? 0xFFFFFFFFu : ((1u << len->bit_size) - 1u);
    return (int64_t)(((uint32_t)data & (mask << len->start_bit)) >> len->start_bit);
}

static void record_put(MiFitnessRecord *r, int type, int64_t value) {
    for (uint8_t i = 0; i < r->count; i++) {
        if (r->values[i].type == type) {
            r->values[i].value = value; /* a later item with the same id wins (HashMap.put) */
            return;
        }
    }
    if (r->count < MI_FITNESS_RECORD_VALUE_MAX) {
        r->values[r->count++] = (MiFitnessRecordValue){type, value};
    }
}

static void four_parse_one(const MiFitnessRecordLayout *l,
                           const FourValid *valid,
                           const uint8_t *body,
                           size_t body_len,
                           size_t *off,
                           MiFitnessRecord *r) {
    for (size_t i = 0; i < l->count; i++) {
        const MiFitnessFourDimenField *f = &l->four[i];
        if (!valid[i].exist) {
            continue;
        }
        if (*off + f->byte_size > body_len) {
            return;
        }
        const int32_t data = read_item(body + *off, f->byte_size, f->is_unsigned != 0);
        *off += f->byte_size;
        if (f->h.present && valid[i].high) {
            record_put(r, f->high, item_value(&f->h, data, f->byte_size));
        }
        if (f->m.present && valid[i].middle && f->middle != MI_FITNESS_NO_TYPE) {
            record_put(r, f->middle, item_value(&f->m, data, f->byte_size));
        }
        if (f->l.present && valid[i].low && f->low != MI_FITNESS_NO_TYPE) {
            record_put(r, f->low, item_value(&f->l, data, f->byte_size));
        }
    }
}

/* ---- OneDimen helpers ---------------------------------------------------------------------- */

/* getOneRecordLeastDataLen: fixed fields only (id >= 0, supported, no dependency). */
static size_t one_least_len(const MiFitnessOneDimenField *f, size_t n, int version) {
    size_t len = 0;
    for (size_t i = 0; f != NULL && i < n; i++) {
        if (f[i].type >= 0 && f[i].support_version <= version && !f[i].has_depend) {
            len += f[i].byte_count;
        }
    }
    return len;
}

/* parsePauseHeaderMoreInfo: a OneDimen block whose validity is just "supported by this version". */
static bool one_parse_header(const MiFitnessOneDimenField *f,
                             size_t n,
                             int version,
                             const uint8_t *body,
                             size_t body_len,
                             size_t *off,
                             MiFitnessFieldValue *vals,
                             size_t *nvals) {
    *nvals = 0;
    if (f == NULL || n == 0) {
        return true;
    }
    return mi_fitness_oneden_parse_one_record(f, n, version, NULL, 0, body, body_len, off, vals, nvals);
}

static void one_to_record(const MiFitnessFieldValue *vals, size_t n, MiFitnessRecord *r) {
    for (size_t i = 0; i < n; i++) {
        if (vals[i].valid) {
            record_put(r, vals[i].type, vals[i].is_float ? (int64_t)vals[i].f32 : vals[i].i64);
        }
    }
}

/* ---- Segments ------------------------------------------------------------------------------ */

typedef struct {
    const MiFitnessRecordParser *parser;
    const MiFitnessRecordLayout *layout;
    int version;
    const uint8_t *valid_bits;
    size_t valid_len;
    FourValid *four_valid; /* layout->count entries, FourDimen only */
    const uint8_t *body;
    size_t body_len;
    GArray *records;
} Ctx;

/* parseLoopRecord: up to `count` records while a whole record still fits; each one second long
 * from `start` unless it carries its own end time (processSportRecordList). */
static void parse_records(Ctx *c, size_t *off, uint32_t start, uint32_t count, uint16_t segment) {
    const MiFitnessRecordLayout *l = c->layout;
    const size_t rec_len = l->four_dimen ? four_record_len(l, c->four_valid) : one_least_len(l->one, l->count, c->version);
    uint32_t t = start;
    /* A layout with nothing present still yields `count` empty one-second records in Mi Fitness;
     * cap those so a garbage count cannot run away. */
    const uint32_t n = rec_len == 0 ? MIN(count, 2u * 86400u) : count;
    for (uint32_t i = 0; i < n && *off + rec_len <= c->body_len; i++) {
        MiFitnessRecord r = {.start_sec = t, .end_sec = t + 1, .segment = segment};
        if (l->four_dimen) {
            four_parse_one(l, c->four_valid, c->body, c->body_len, off, &r);
        } else {
            MiFitnessFieldValue vals[MI_FITNESS_FIELD_VALUE_MAX];
            size_t nv = 0;
            if (!mi_fitness_oneden_parse_one_record(l->one, l->count, c->version, c->valid_bits, c->valid_len, c->body,
                                                    c->body_len, off, vals, &nv)) {
                return;
            }
            one_to_record(vals, nv, &r);
        }
        int64_t end = 0;
        if (mi_fitness_record_get(&r, 1, &end)) { /* the record's own end time, taken as is */
            r.end_sec = (uint32_t)end;
        }
        t = r.end_sec;
        g_array_append_val(c->records, r);
    }
}

static void segment_add(GArray *segments, uint32_t start, uint32_t count, const MiFitnessFieldValue *init, size_t ninit) {
    if (segments == NULL) {
        return;
    }
    MiFitnessRecordSegment s = {.start_sec = start, .record_count = count};
    for (size_t i = 0; i < ninit; i++) {
        if (init[i].valid && init[i].type == 0) { /* setInitDataValue: 0 = start altitude */
            s.has_height = true;
            s.height_m = init[i].is_float ? init[i].f32 : (float)init[i].i64;
        }
    }
    g_array_append_val(segments, s);
}

/* SportRecordBaseParser.parseFourDimenData / parseOneDimenData. */
static void parse_segments(Ctx *c, GArray *segments) {
    const MiFitnessRecordParser *p = c->parser;
    const size_t head = one_least_len(p->init, p->init_count, c->version) + 8 +
                        one_least_len(p->it_summary, p->it_summary_count, c->version);
    size_t off = 0;
    uint16_t seg = 0;
    while (off + head <= c->body_len) {
        MiFitnessFieldValue init[MI_FITNESS_FIELD_VALUE_MAX], it[MI_FITNESS_FIELD_VALUE_MAX];
        size_t ninit = 0, nit = 0;
        if (!one_parse_header(p->init, p->init_count, c->version, c->body, c->body_len, &off, init, &ninit) ||
            off + 8 > c->body_len) {
            return;
        }
        const uint32_t count = miwear_read_u32_le(c->body + off);
        const uint32_t start = miwear_read_u32_le(c->body + off + 4);
        off += 8;
        if (!one_parse_header(p->it_summary, p->it_summary_count, c->version, c->body, c->body_len, &off, it, &nit)) {
            return;
        }
        segment_add(segments, start, count, init, ninit);
        parse_records(c, &off, start, count, seg++);
    }
}

/* GroupSportRecordParser: sets of exercise + rest records, without init data or summary. */
static void parse_group_segments(Ctx *c, GArray *segments) {
    size_t off = 0;
    uint16_t seg = 0;
    uint64_t done = 0; /* records of the current group so far */
    while (off + 13 <= c->body_len) {
        const uint32_t count = miwear_read_u32_le(c->body + off);
        const uint32_t start = miwear_read_u32_le(c->body + off + 4);
        const bool is_group = c->body[off + 8] > 0;
        const uint32_t group_count = miwear_read_u32_le(c->body + off + 9);
        off += 13;
        segment_add(segments, start, count, NULL, 0);
        parse_records(c, &off, start, count, seg++);
        done += count;
        if (is_group) {
            if (group_count > done) { /* the rest period follows the group's last set */
                const uint32_t rest = (uint32_t)(group_count - done);
                segment_add(segments, start + count, rest, NULL, 0);
                parse_records(c, &off, start + count, rest, seg++);
            }
            done = 0;
        }
    }
}

bool mi_fitness_sport_record_decode(const uint8_t *payload,
                                    size_t payload_len,
                                    int sport_type,
                                    int version,
                                    GArray *records,
                                    GArray *segments,
                                    const char **parser_out) {
    if (payload == NULL || records == NULL || payload_len < 12) {
        return false;
    }
    const MiFitnessRecordParser *p = mi_fitness_record_parser(sport_type);
    const MiFitnessRecordLayout *l = mi_fitness_record_layout(p, version);
    if (l == NULL) {
        return false;
    }
    MiwearFitnessDataId id = {.data_type = 1, .file_type = 0, .sport_type = (uint8_t)sport_type, .version = (uint8_t)version};
    const int valid_len = mi_fitness_data_validity_len(&id);
    const size_t wire_len = payload_len - 4; /* trailing CRC32 */
    if (valid_len < 0 || wire_len < 8 + (size_t)valid_len) {
        return false;
    }
    if (parser_out != NULL) {
        *parser_out = p->parser;
    }

    FourValid fv[64];
    if (l->four_dimen) {
        if (l->count > G_N_ELEMENTS(fv)) {
            return false;
        }
        four_valid(l, version, payload + 8, (size_t)valid_len, fv);
    }
    Ctx c = {
        .parser = p,
        .layout = l,
        .version = version,
        .valid_bits = payload + 8,
        .valid_len = (size_t)valid_len,
        .four_valid = fv,
        .body = payload + 8 + valid_len,
        .body_len = wire_len - 8 - (size_t)valid_len,
        .records = records,
    };
    const guint before = records->len;
    /* GroupSportRecordParser only frames groups when the version has no summary block. */
    if (p->group && one_least_len(p->it_summary, p->it_summary_count, version) == 0) {
        parse_group_segments(&c, segments);
    } else {
        parse_segments(&c, segments);
    }
    return records->len > before;
}

bool mi_fitness_record_get(const MiFitnessRecord *r, int type, int64_t *value) {
    for (uint8_t i = 0; r != NULL && i < r->count; i++) {
        if (r->values[i].type == type) {
            if (value != NULL) {
                *value = r->values[i].value;
            }
            return true;
        }
    }
    return false;
}

/* ---- Interpretation (setSportRecordItemValue) ---------------------------------------------- */

static void set(MiwearWorkoutSample *s, MiwearRecordSeries k, double v) {
    s->v[k] = (float)v;
}

void mi_fitness_sport_record_apply(int sport_type, const GArray *records, const GArray *segments, MiwearWorkoutSummary *out) {
    if (records == NULL || out == NULL || records->len == 0) {
        return;
    }
    const MiFitnessRecordParser *p = mi_fitness_record_parser(sport_type);
    const double dist_div = p != NULL && p->distance_times > 0 ? p->distance_times : 10;
    const uint32_t t0 = g_array_index(records, MiFitnessRecord, 0).start_sec;

    miwear_workout_summary_free_series(out);
    out->record_samples = g_array_sized_new(FALSE, FALSE, sizeof(MiwearWorkoutSample), records->len);
    int seg = -1;
    double height = NAN; /* SportRecord.height: running altitude of the segment */
    for (guint i = 0; i < records->len; i++) {
        const MiFitnessRecord *r = &g_array_index(records, MiFitnessRecord, i);
        if (r->segment != seg) {
            seg = r->segment;
            height = NAN;
            if (segments != NULL && (guint)seg < segments->len) {
                const MiFitnessRecordSegment *sg = &g_array_index(segments, MiFitnessRecordSegment, seg);
                if (sg->has_height) {
                    height = sg->height_m;
                }
            }
        }
        /* Lap records (swimming) are placed at their end; one-second records at their start. */
        const uint32_t at = r->end_sec > r->start_sec + 1 ? r->end_sec : r->start_sec;
        MiwearWorkoutSample s = miwear_workout_sample_init(at >= t0 ? at - t0 : 0);
        int64_t v = 0;
        if (mi_fitness_record_get(r, 5, &v) && v > 0 && v < 256) {
            s.hr = (uint8_t)v;
        }
        if (mi_fitness_record_get(r, 4, &v) && v > 0) {
            s.steps_delta = (uint16_t)MIN(v, 65535);
        }
        if (mi_fitness_record_get(r, 2, &v)) {
            set(&s, MIWEAR_SERIES_CALORIES, (double)v);
        }
        if (mi_fitness_record_get(r, 9, &v) || mi_fitness_record_get(r, 88, &v)) {
            set(&s, MIWEAR_SERIES_DISTANCE, (double)v / dist_div);
        }
        if (mi_fitness_record_get(r, 12, &v) && v > 0) {
            set(&s, MIWEAR_SERIES_PACE, (double)v);
        }
        if (mi_fitness_record_get(r, 51, &v)) {
            set(&s, MIWEAR_SERIES_SPEED, (double)v / 10.0);
        }
        int64_t dir = 0;
        if (mi_fitness_record_get(r, 74, &v)) { /* diving vertical speed; 73: 1 = up, 0 = down, else 0 */
            const bool has_dir = mi_fitness_record_get(r, 73, &dir);
            const double sign = !has_dir ? 0.0 : dir == 1 ? 1.0 : dir == 0 ? -1.0 : 0.0;
            set(&s, MIWEAR_SERIES_SPEED, sign * (double)v / 10.0);
        }
        static const int cadence_ids[] = {49, 50, 52, 53};
        for (size_t k = 0; k < G_N_ELEMENTS(cadence_ids); k++) {
            if (mi_fitness_record_get(r, cadence_ids[k], &v)) {
                set(&s, MIWEAR_SERIES_CADENCE, (double)v);
            }
        }
        if (mi_fitness_record_get(r, 47, &v) || mi_fitness_record_get(r, 57, &v)) {
            set(&s, MIWEAR_SERIES_POWER, (double)v);
        }
        if (mi_fitness_record_get(r, 40, &v)) {
            set(&s, MIWEAR_SERIES_STRIDE, (double)v);
        }
        if (mi_fitness_record_get(r, 38, &v) && v > 0) {
            set(&s, MIWEAR_SERIES_SPO2, (double)v);
        }
        if (mi_fitness_record_get(r, 39, &v) && v > 0) {
            set(&s, MIWEAR_SERIES_STRESS, (double)v);
        }
        if (mi_fitness_record_get(r, 13, &v) && v > 0) {
            set(&s, MIWEAR_SERIES_SWOLF, (double)v);
        }
        if (mi_fitness_record_get(r, 17, &v) && v > 0) {
            set(&s, MIWEAR_SERIES_STROKE_RATE, (double)v);
        }
        if (mi_fitness_record_get(r, 45, &v) && v > 0) {
            set(&s, MIWEAR_SERIES_GROUND_CONTACT, (double)v);
        }
        if (mi_fitness_record_get(r, 80, &v) && v > 0) {
            set(&s, MIWEAR_SERIES_VERTICAL_OSC, (double)v / 10.0);
        }
        if (mi_fitness_record_get(r, 71, &v)) {
            set(&s, MIWEAR_SERIES_DEPTH, (double)v / 10.0);
        }
        if (mi_fitness_record_get(r, 72, &v)) {
            set(&s, MIWEAR_SERIES_WATER_TEMP, (double)v / 10.0);
        }
        /* Altitude: 87 is absolute (dm, 65535 = unknown); 8 is a change (dm) from the running
         * altitude, climbing when 7 is non-zero. Key 7 alone does nothing (in the bytecode it jumps
         * straight to return; jadx's normal output wrongly shows it falling into case 8). */
        if (mi_fitness_record_get(r, 87, &v)) {
            if (v != 65535) {
                height = (double)v / 10.0;
            }
            if (!isnan(height)) {
                set(&s, MIWEAR_SERIES_ALTITUDE, height);
            }
        } else if (!isnan(height) && mi_fitness_record_get(r, 8, &v) && mi_fitness_record_get(r, 7, &dir)) {
            height += (dir != 0 ? 1.0 : -1.0) * (double)v / 10.0;
            set(&s, MIWEAR_SERIES_ALTITUDE, height);
        }
        g_array_append_val(out->record_samples, s);
    }

    /* Typical spacing between samples (1 s, or the average lap time for swimming). */
    const MiwearWorkoutSample *last = &g_array_index(out->record_samples, MiwearWorkoutSample, out->record_samples->len - 1);
    out->record_interval_sec = out->record_samples->len > 1
                                   ? MAX(1, (int)(last->offset_sec / (out->record_samples->len - 1)))
                                   : 1;
}
