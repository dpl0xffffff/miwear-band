#include "miwear/activity/mi_fitness_full_day_sleep.h"
#include "miwear/util.h"

static size_t full_day_sleep_crc_end(const uint8_t *payload, size_t payload_len) {
    return payload_len >= 4 ? payload_len - 4 : payload_len;
}

static size_t full_day_sleep_vf_len(int version) {
    if (version == 5) {
        return 2;
    }
    if (version == 6) {
        return 3;
    }
    return 0;
}

gboolean miwear_full_day_sleep_valid_bit(const uint8_t *vf, size_t vf_len, int field_index) {
    if (vf == NULL || field_index < 0 || field_index > 10) {
        return FALSE;
    }
    const int msb = (int)(vf_len * 8u - 1u);
    const int bit = msb - field_index;
    if (bit < 0) {
        return FALSE;
    }
    const size_t byte = (size_t)(bit / 8);
    const int bit_in_byte = 7 - (bit % 8);
    if (byte >= vf_len) {
        return FALSE;
    }
    return (vf[byte] & (1u << bit_in_byte)) != 0;
}

static size_t skip_hr_or_spo2_series(const uint8_t *payload,
                                     size_t payload_len,
                                     size_t off,
                                     uint16_t unit,
                                     uint16_t count) {
    (void)unit;
    const size_t crc_end = full_day_sleep_crc_end(payload, payload_len);
    if (count == 0 || off >= crc_end) {
        return off;
    }
    if (off + 4 <= crc_end) {
        off += 4; /* first timestamp optional in stream */
    }
    if (off + (size_t)count <= crc_end) {
        off += (size_t)count;
    } else {
        off = crc_end;
    }
    return off;
}

size_t miwear_full_day_sleep_structured_end(const uint8_t *payload, size_t payload_len, int version) {
    const size_t crc_end = full_day_sleep_crc_end(payload, payload_len);
    const size_t vf_len = full_day_sleep_vf_len(version);
    if (vf_len == 0 || crc_end < 10 + 1) {
        return 0;
    }

    const uint8_t *vf = payload + 8;
    size_t off = 8 + vf_len + 1; /* sleepFull */

    if (miwear_full_day_sleep_valid_bit(vf, vf_len, 0)) {
        off += 4;
    }
    if (miwear_full_day_sleep_valid_bit(vf, vf_len, 1)) {
        off += 4;
    }
    if (miwear_full_day_sleep_valid_bit(vf, vf_len, 2)) {
        off += 1;
    }
    if (miwear_full_day_sleep_valid_bit(vf, vf_len, 3)) {
        off += 1;
    }
    if (miwear_full_day_sleep_valid_bit(vf, vf_len, 4)) {
        off += 4;
    }
    if (miwear_full_day_sleep_valid_bit(vf, vf_len, 5)) {
        off += 4;
    }
    if (miwear_full_day_sleep_valid_bit(vf, vf_len, 6)) {
        off += 4;
    }
    if (miwear_full_day_sleep_valid_bit(vf, vf_len, 7)) {
        off += 4;
    }

    if (miwear_full_day_sleep_valid_bit(vf, vf_len, 8) && off + 4 <= crc_end) {
        const uint16_t unit = miwear_read_u16_le(payload + off);
        off += 2;
        const uint16_t count = miwear_read_u16_le(payload + off);
        off += 2;
        off = skip_hr_or_spo2_series(payload, payload_len, off, unit, count);
    }
    if (miwear_full_day_sleep_valid_bit(vf, vf_len, 9) && off + 4 <= crc_end) {
        const uint16_t unit = miwear_read_u16_le(payload + off);
        off += 2;
        const uint16_t count = miwear_read_u16_le(payload + off);
        off += 2;
        off = skip_hr_or_spo2_series(payload, payload_len, off, unit, count);
    }
    if (miwear_full_day_sleep_valid_bit(vf, vf_len, 10) && off + 4 <= crc_end) {
        off += 2;
        const uint16_t count = miwear_read_u16_le(payload + off);
        off += 2;
        if (count > 0 && off + 4 <= crc_end) {
            off += 4;
        }
        const size_t skip = (size_t)count * 4u;
        if (off + skip <= crc_end) {
            off += skip;
        } else {
            off = crc_end;
        }
    }

    if (off > crc_end) {
        return crc_end;
    }
    return off;
}

gboolean mi_fitness_full_day_sleep_bed_wake(const uint8_t *payload,
                                            size_t payload_len,
                                            int version,
                                            uint32_t *bed_sec_out,
                                            uint32_t *wake_sec_out) {
    if (payload == NULL || bed_sec_out == NULL || wake_sec_out == NULL) {
        return FALSE;
    }
    *bed_sec_out = 0;
    *wake_sec_out = 0;
    const size_t crc_end = full_day_sleep_crc_end(payload, payload_len);
    const size_t vf_len = full_day_sleep_vf_len(version);
    if (vf_len == 0 || crc_end < 10 + 1) {
        return FALSE;
    }
    const uint8_t *vf = payload + 8;
    size_t off = 8 + vf_len + 1;
    uint32_t start = 0;
    uint32_t end = 0;
    uint32_t go_bed = 0;
    uint32_t leave = 0;
    if (miwear_full_day_sleep_valid_bit(vf, vf_len, 0) && off + 4 <= crc_end) {
        start = miwear_read_u32_le(payload + off);
        off += 4;
    }
    if (miwear_full_day_sleep_valid_bit(vf, vf_len, 1) && off + 4 <= crc_end) {
        end = miwear_read_u32_le(payload + off);
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
        leave = miwear_read_u32_le(payload + off);
        off += 4;
    }
    *bed_sec_out = go_bed != 0 ? go_bed : start;
    *wake_sec_out = leave != 0 ? leave : end;
    return *bed_sec_out != 0 && *wake_sec_out != 0 && *wake_sec_out > *bed_sec_out;
}

gboolean mi_fitness_full_day_sleep_feature_tail(const uint8_t *payload,
                                                size_t payload_len,
                                                int version,
                                                const uint8_t **feature_out,
                                                size_t *feature_len_out) {
    if (payload == NULL || feature_out == NULL || feature_len_out == NULL) {
        return FALSE;
    }
    *feature_out = NULL;
    *feature_len_out = 0;
    const size_t crc_end = full_day_sleep_crc_end(payload, payload_len);
    const size_t off = miwear_full_day_sleep_structured_end(payload, payload_len, version);
    if (off == 0 || off >= crc_end) {
        return FALSE;
    }
    *feature_out = payload + off;
    *feature_len_out = crc_end - off;
    return *feature_len_out > 0;
}
