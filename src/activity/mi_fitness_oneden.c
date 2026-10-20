#include "miwear/activity/mi_fitness_oneden.h"
#include "miwear/util.h"

#include <string.h>

static bool build_valid_map(const MiFitnessOneDimenField *fields,
                            size_t field_count,
                            int data_version,
                            const uint8_t *data_valid,
                            size_t data_valid_len,
                            bool valid_map[256]) {
    memset(valid_map, 0, 256);
    if (data_valid_len == 0) {
        for (size_t i = 0; i < field_count; i++) {
            const MiFitnessOneDimenField *f = &fields[i];
            if (f->support_version <= (uint8_t)data_version && f->type >= 0 && f->type < 256) {
                valid_map[f->type] = true;
            }
        }
        return true;
    }
    size_t bit_index = 0;
    for (size_t i = 0; i < field_count; i++) {
        const MiFitnessOneDimenField *f = &fields[i];
        bool valid = false;
        if (f->support_version > (uint8_t)data_version) {
            valid = false;
        } else {
            const size_t byte_i = bit_index / 8;
            const size_t bit_i = bit_index % 8;
            if (byte_i >= data_valid_len) {
                return false;
            }
            valid = (data_valid[byte_i] & (1u << (7 - bit_i))) != 0;
            bit_index++;
        }
        if (f->type >= 0 && f->type < 256) {
            valid_map[f->type] = valid;
        }
    }
    return true;
}

static const MiFitnessFieldValue *parsed_get(const MiFitnessFieldValue *parsed, size_t parsed_count, int type) {
    for (size_t i = 0; i < parsed_count; i++) {
        if (parsed[i].type == type) {
            return &parsed[i];
        }
    }
    return NULL;
}

static bool read_value(const MiFitnessOneDimenField *f,
                       bool valid,
                       int repeat_count,
                       const uint8_t *body,
                       size_t body_len,
                       size_t *off,
                       MiFitnessFieldValue *out) {
    out->type = f->type;
    out->valid = valid;
    out->is_float = f->is_float != 0;
    out->i64 = 0;
    out->f32 = 0.0f;

    if (repeat_count >= 0) {
        const size_t need = (size_t)f->byte_count * (size_t)repeat_count;
        if (*off + need > body_len) {
            return false;
        }
        *off += need;
        out->i64 = repeat_count;
        return true;
    }

    if (f->byte_count == 0) {
        return true;
    }
    if (*off + f->byte_count > body_len) {
        return false;
    }

    const uint8_t *p = body + *off;
    switch (f->byte_count) {
        case 1:
            out->i64 = f->is_unsigned ? p[0] : (int8_t)p[0];
            break;
        case 2: {
            const uint16_t v = miwear_read_u16_le(p);
            out->i64 = f->is_unsigned ? v : (int16_t)v;
            break;
        }
        case 4:
            if (f->is_float) {
                uint32_t raw = miwear_read_u32_le(p);
                memcpy(&out->f32, &raw, sizeof(float));
                out->is_float = true;
            } else {
                const uint32_t v = miwear_read_u32_le(p);
                out->i64 = f->is_unsigned ? (int64_t)v : (int64_t)(int32_t)v;
            }
            break;
        case 8: {
            const uint64_t v = (uint64_t)miwear_read_u32_le(p) | ((uint64_t)miwear_read_u32_le(p + 4) << 32);
            out->i64 = (int64_t)v;
            break;
        }
        default:
            *off += f->byte_count;
            return true;
    }
    *off += f->byte_count;
    return true;
}

static bool depend_value_matches(int profile, int64_t value) {
    if (profile == MI_FITNESS_DEPEND_COURSE) {
        return value == 1;
    }
    if (profile == MI_FITNESS_DEPEND_LACTATE) {
        return value == 2 || value == 3;
    }
    return false;
}

static bool field_exists(const MiFitnessOneDimenField *f,
                         int data_version,
                         const MiFitnessFieldValue *parsed,
                         size_t parsed_count) {
    if (f->support_version > (uint8_t)data_version) {
        return false;
    }
    if (!f->has_depend) {
        return true;
    }
    const MiFitnessFieldValue *ref = parsed_get(parsed, parsed_count, 205);
    if (ref == NULL || !ref->valid) {
        return false;
    }
    return depend_value_matches(f->depend_profile, ref->i64);
}

size_t mi_fitness_oneden_one_record_body_len(const MiFitnessOneDimenField *fields,
                                             size_t field_count,
                                             int data_version) {
    if (fields == NULL) {
        return 0;
    }
    size_t total = 0;
    for (size_t i = 0; i < field_count; i++) {
        const MiFitnessOneDimenField *f = &fields[i];
        if (f->support_version > (uint8_t)data_version) {
            continue;
        }
        if (f->byte_count > 0) {
            total += (size_t)f->byte_count;
        }
    }
    return total;
}

bool mi_fitness_oneden_parse_one_record(const MiFitnessOneDimenField *fields,
                                        size_t field_count,
                                        int data_version,
                                        const uint8_t *data_valid,
                                        size_t data_valid_len,
                                        const uint8_t *body,
                                        size_t body_len,
                                        size_t *inout_off,
                                        MiFitnessFieldValue *out_values,
                                        size_t *out_count) {
    if (fields == NULL || body == NULL || inout_off == NULL || out_values == NULL || out_count == NULL) {
        return false;
    }

    bool valid_map[256];
    if (!build_valid_map(fields, field_count, data_version, data_valid, data_valid_len, valid_map)) {
        return false;
    }

    MiFitnessFieldValue scratch[MI_FITNESS_FIELD_VALUE_MAX];
    size_t scratch_count = 0;
    size_t off = *inout_off;

    for (size_t i = 0; i < field_count; i++) {
        const MiFitnessOneDimenField *f = &fields[i];
        if (!field_exists(f, data_version, scratch, scratch_count)) {
            continue;
        }
        const bool valid = valid_map[(uint8_t)f->type];
        int repeat = -1;
        if (f->counts_type >= 0) {
            const MiFitnessFieldValue *cnt = parsed_get(scratch, scratch_count, f->counts_type);
            repeat = cnt != NULL ? (int)cnt->i64 : 0;
        }
        if (scratch_count >= MI_FITNESS_FIELD_VALUE_MAX) {
            break;
        }
        if (!read_value(f, valid, repeat, body, body_len, &off, &scratch[scratch_count])) {
            return false;
        }
        scratch_count++;
    }

    size_t n = 0;
    for (size_t i = 0; i < scratch_count && n < MI_FITNESS_FIELD_VALUE_MAX; i++) {
        out_values[n++] = scratch[i];
    }
    *out_count = n;
    *inout_off = off;
    return true;
}

size_t mi_fitness_oneden_parse_loop_records(const MiFitnessOneDimenField *fields,
                                            size_t field_count,
                                            int data_version,
                                            const uint8_t *data_valid,
                                            size_t data_valid_len,
                                            const uint8_t *body,
                                            size_t body_len) {
    const size_t min_len = mi_fitness_oneden_one_record_body_len(fields, field_count, data_version);
    if (min_len == 0 || body == NULL) {
        return 0;
    }
    size_t off = 0;
    size_t count = 0;
    while (off + min_len <= body_len) {
        MiFitnessFieldValue tmp[MI_FITNESS_FIELD_VALUE_MAX];
        size_t n = 0;
        if (!mi_fitness_oneden_parse_one_record(fields,
                                                field_count,
                                                data_version,
                                                data_valid,
                                                data_valid_len,
                                                body,
                                                body_len,
                                                &off,
                                                tmp,
                                                &n)) {
            break;
        }
        count++;
    }
    return count;
}

bool mi_fitness_oneden_parse_report(const MiFitnessOneDimenField *fields,
                                    size_t field_count,
                                    int data_version,
                                    const uint8_t *data_valid,
                                    size_t data_valid_len,
                                    const uint8_t *body,
                                    size_t body_len,
                                    MiFitnessFieldValue *out_values,
                                    size_t *out_count) {
    if (fields == NULL || data_valid == NULL || body == NULL || out_values == NULL || out_count == NULL) {
        return false;
    }

    bool valid_map[256];
    if (!build_valid_map(fields, field_count, data_version, data_valid, data_valid_len, valid_map)) {
        return false;
    }

    MiFitnessFieldValue scratch[MI_FITNESS_FIELD_VALUE_MAX];
    size_t scratch_count = 0;
    size_t off = 0;

    for (size_t i = 0; i < field_count; i++) {
        const MiFitnessOneDimenField *f = &fields[i];
        if (!field_exists(f, data_version, scratch, scratch_count)) {
            continue;
        }
        const bool valid = valid_map[(uint8_t)f->type];
        int repeat = -1;
        if (f->counts_type >= 0) {
            const MiFitnessFieldValue *cnt = parsed_get(scratch, scratch_count, f->counts_type);
            repeat = cnt != NULL ? (int)cnt->i64 : 0;
        }
        if (scratch_count >= MI_FITNESS_FIELD_VALUE_MAX) {
            break;
        }
        if (!read_value(f, valid, repeat, body, body_len, &off, &scratch[scratch_count])) {
            break;
        }
        scratch_count++;
    }

    size_t n = 0;
    for (size_t i = 0; i < scratch_count && n < MI_FITNESS_FIELD_VALUE_MAX; i++) {
        if (scratch[i].valid && scratch[i].type >= 0) {
            out_values[n++] = scratch[i];
        }
    }
    *out_count = n;
    return n > 0;
}
