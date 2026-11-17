#include "miwear/activity/daily_details.h"

#include <string.h>

/*
 * Port of Gadgetbridge's XiaomiComplexActivityParser + DailyDetailsParser
 * (service/devices/xiaomi/activity/impl). Each record is a sequence of groups; the file header
 * holds one nibble per group: bit 3 = group present (bytes follow), bits 2..0 = which fields of
 * the group are valid.
 */

typedef struct {
    const uint8_t *header;
    size_t header_len;
    const uint8_t *buf;
    size_t pos;
    size_t limit;
    int group;
    int group_bits;
    uint32_t val;
    gboolean underflow;
} ComplexParser;

static uint32_t consume(ComplexParser *p, int nbits) {
    const size_t n = (size_t)nbits / 8u;
    if (p->pos + n > p->limit) {
        p->underflow = TRUE;
        p->pos = p->limit;
        return 0;
    }
    uint32_t v = 0;
    for (size_t i = 0; i < n; i++) {
        v |= (uint32_t)p->buf[p->pos + i] << (8 * i); /* little-endian */
    }
    p->pos += n;
    return v;
}

static int nibble(const ComplexParser *p) {
    const uint8_t b = p->header[p->group / 2];
    return (p->group % 2 == 0) ? (b & 0xf0) >> 4 : b & 0x0f;
}

static void reset(ComplexParser *p) {
    p->group = -1;
    p->group_bits = 0;
    p->val = 0;
}

static gboolean next_group(ComplexParser *p, int nbits) {
    p->group++;
    if ((size_t)p->group >= p->header_len * 2) {
        /* Gadgetbridge consumes anyway so the buffer still advances. */
        (void)consume(p, nbits);
        return FALSE;
    }
    if ((nibble(p) & 8) == 0) {
        return FALSE; /* group absent: nothing to consume */
    }
    p->group_bits = nbits;
    p->val = consume(p, nbits);
    return !p->underflow;
}

static gboolean valid(const ComplexParser *p, int idx) {
    return (nibble(p) & (1 << (2 - idx))) != 0;
}

static int get(const ComplexParser *p, int idx, int nbits) {
    const int shift = p->group_bits - idx - nbits;
    return (int)((p->val >> shift) & ((1u << nbits) - 1u));
}

int miwear_daily_details_header_size(int version) {
    switch (version) {
        case 1:
        case 2:
            return 4;
        case 3:
            return 5;
        case 4:
            return 6;
        default:
            return 0;
    }
}

GArray *miwear_daily_details_decode(int version, const uint8_t *payload, size_t payload_len, const char **error_out) {
    const int header_size = miwear_daily_details_header_size(version);
    if (header_size == 0) {
        if (error_out != NULL) {
            *error_out = "unsupported daily details version";
        }
        return NULL;
    }
    /* 7-byte file id + 1 padding byte, header, records, 4-byte CRC32. */
    if (payload == NULL || payload_len < (size_t)(8 + header_size + 4)) {
        if (error_out != NULL) {
            *error_out = "payload too short";
        }
        return NULL;
    }

    ComplexParser p = {
        .header = payload + 8,
        .header_len = (size_t)header_size,
        .buf = payload,
        .pos = (size_t)(8 + header_size),
        .limit = payload_len - 4,
    };
    GArray *out = g_array_new(FALSE, FALSE, sizeof(MiwearDailyDetailsMinute));

    while (p.pos < p.limit && !p.underflow) {
        const size_t before = p.pos;
        reset(&p);
        MiwearDailyDetailsMinute m = {-1, -1, -1, -1, -1, -1, -1};

        int include_extra_entry = 0;
        if (next_group(&p, 16)) {
            if (valid(&p, 1)) {
                include_extra_entry = get(&p, 1, 1);
            }
            if (valid(&p, 2)) {
                m.steps = get(&p, 2, 14);
            }
        }
        if (next_group(&p, 8) && valid(&p, 1)) {
            m.active_calories = get(&p, 2, 6);
        }
        (void)next_group(&p, 8);
        if (next_group(&p, 16) && valid(&p, 0)) {
            m.distance_cm = get(&p, 0, 16) * 100;
        }
        if (next_group(&p, 8) && valid(&p, 0)) {
            m.heart_rate = get(&p, 0, 8);
        }
        if (next_group(&p, 8) && valid(&p, 0)) {
            m.energy = get(&p, 0, 8);
        }
        (void)next_group(&p, 16);
        if (version >= 3) {
            if (next_group(&p, 8) && valid(&p, 0)) {
                m.spo2 = get(&p, 0, 8);
            }
            if (next_group(&p, 8) && valid(&p, 0)) {
                const int stress = get(&p, 0, 8);
                m.stress = stress != 255 ? stress : -1;
            }
        }
        if (include_extra_entry == 1) {
            (void)consume(&p, 8); /* Gadgetbridge #4625: a byte is always present */
        }
        if (version >= 4) {
            (void)next_group(&p, 16); /* light */
            (void)next_group(&p, 16); /* body momentum */
        }
        if (p.underflow) {
            break; /* truncated last record: drop it rather than store a partial minute */
        }
        if (p.pos == before) {
            break; /* header with no present groups: no progress possible */
        }
        g_array_append_val(out, m);
    }
    return out;
}
