#include "miwear/activity/mi_fitness_full_day_sleep.h"
#include "miwear/util.h"

#include <string.h>

static size_t full_day_sleep_crc_end(size_t payload_len) {
    return payload_len >= 4 ? payload_len - 4 : payload_len;
}

gboolean miwear_full_day_sleep_valid_bit(const uint8_t *vf, size_t vf_len, int field_index) {
    if (vf == NULL || vf_len == 0 || field_index < 0 || (size_t)field_index >= vf_len * 8u) {
        return FALSE;
    }
    const size_t byte = (size_t)field_index / 8u;
    return (vf[byte] & (0x80u >> (field_index % 8))) != 0;
}

/* Validity-bit indices (schema fieldOrder) per version. */
typedef struct {
    size_t vf_len;
    int hrv_summary_first; /* -1 when absent */
    int heart_rate;
    int spo2;
    int hrv_series; /* -1 when absent */
    int snore;
} FullDaySleepBits;

static gboolean full_day_sleep_bits(int version, FullDaySleepBits *bits) {
    if (version == 5) {
        *bits = (FullDaySleepBits){.vf_len = 2, .hrv_summary_first = -1, .heart_rate = 8, .spo2 = 9,
                                   .hrv_series = -1, .snore = 10};
        return TRUE;
    }
    if (version == 6) {
        *bits = (FullDaySleepBits){.vf_len = 3, .hrv_summary_first = 8, .heart_rate = 19, .spo2 = 20,
                                   .hrv_series = 21, .snore = 22};
        return TRUE;
    }
    return FALSE;
}

typedef struct {
    const uint8_t *p;
    size_t off;
    size_t end;
    gboolean ok;
} Cursor;

static gboolean cur_take(Cursor *c, size_t n) {
    if (!c->ok || c->off + n > c->end) {
        c->ok = FALSE;
        return FALSE;
    }
    c->off += n;
    return TRUE;
}

static uint32_t cur_u32(Cursor *c) {
    const size_t at = c->off;
    return cur_take(c, 4) ? miwear_read_u32_le(c->p + at) : 0;
}

static uint16_t cur_u16(Cursor *c) {
    const size_t at = c->off;
    return cur_take(c, 2) ? miwear_read_u16_le(c->p + at) : 0;
}

/* interval (u16), count (u16), firstTime (u32, only when count > 0), values. */
static void cur_series(Cursor *c, gboolean valid, size_t value_size, MiwearFullDaySleepSeries *s) {
    memset(s, 0, sizeof(*s));
    if (!valid || !c->ok) {
        return;
    }
    s->present = TRUE;
    s->interval_sec = cur_u16(c);
    s->count = cur_u16(c);
    if (s->count > 0) {
        s->first_time = cur_u32(c);
    }
    s->values_off = c->off;
    (void)cur_take(c, (size_t)s->count * value_size);
}

gboolean miwear_full_day_sleep_layout(const uint8_t *payload,
                                      size_t payload_len,
                                      int version,
                                      MiwearFullDaySleepLayout *out) {
    if (payload == NULL || out == NULL) {
        return FALSE;
    }
    memset(out, 0, sizeof(*out));
    FullDaySleepBits bits;
    if (!full_day_sleep_bits(version, &bits)) {
        return FALSE;
    }
    out->crc_end = full_day_sleep_crc_end(payload_len);
    const uint8_t *vf = payload + 8;
    Cursor c = {.p = payload, .off = 8, .end = out->crc_end, .ok = TRUE};
    if (!cur_take(&c, bits.vf_len)) {
        return FALSE;
    }
#define VALID(i) miwear_full_day_sleep_valid_bit(vf, bits.vf_len, (i))
    if (cur_take(&c, 1)) {
        out->sleep_full = payload[c.off - 1];
    }
    if (VALID(0)) out->start_time = cur_u32(&c);
    if (VALID(1)) out->end_time = cur_u32(&c);
    if (VALID(2)) (void)cur_take(&c, 1); /* sleepQuality */
    if (VALID(3)) (void)cur_take(&c, 1); /* sleepEfficiency */
    if (VALID(4)) (void)cur_take(&c, 4); /* asleepDuration */
    if (VALID(5)) (void)cur_take(&c, 4); /* liebBedDuration */
    if (VALID(6)) out->go_bed_time = cur_u32(&c);
    if (VALID(7)) out->live_bed_time = cur_u32(&c);
    if (bits.hrv_summary_first >= 0) {
        /* hrvAvg, SD, median, lower/upper/middle percentile (u16), timestamp (u32),
         * max, min, baseline max, baseline min (u16). */
        static const size_t hrv_sizes[11] = {2, 2, 2, 2, 2, 2, 4, 2, 2, 2, 2};
        for (int i = 0; i < 11; i++) {
            if (VALID(bits.hrv_summary_first + i)) {
                (void)cur_take(&c, hrv_sizes[i]);
            }
        }
    }
    cur_series(&c, VALID(bits.heart_rate), 1, &out->heart_rate);
    cur_series(&c, VALID(bits.spo2), 1, &out->spo2);
    if (bits.hrv_series >= 0) {
        cur_series(&c, VALID(bits.hrv_series), 2, &out->hrv);
    }
    cur_series(&c, VALID(bits.snore), 4, &out->snore);
#undef VALID
    out->feature_off = c.off;
    return c.ok;
}

size_t miwear_full_day_sleep_structured_end(const uint8_t *payload, size_t payload_len, int version) {
    MiwearFullDaySleepLayout l;
    return miwear_full_day_sleep_layout(payload, payload_len, version, &l) ? l.feature_off : 0;
}

gboolean mi_fitness_full_day_sleep_bed_wake(const uint8_t *payload,
                                            size_t payload_len,
                                            int version,
                                            uint32_t *bed_sec_out,
                                            uint32_t *wake_sec_out) {
    if (bed_sec_out == NULL || wake_sec_out == NULL) {
        return FALSE;
    }
    *bed_sec_out = 0;
    *wake_sec_out = 0;
    MiwearFullDaySleepLayout l;
    (void)miwear_full_day_sleep_layout(payload, payload_len, version, &l); /* the times precede any series */
    *bed_sec_out = l.go_bed_time != 0 ? l.go_bed_time : l.start_time;
    *wake_sec_out = l.live_bed_time != 0 ? l.live_bed_time : l.end_time;
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
    MiwearFullDaySleepLayout l;
    if (!miwear_full_day_sleep_layout(payload, payload_len, version, &l) || l.feature_off >= l.crc_end) {
        return FALSE;
    }
    *feature_out = payload + l.feature_off;
    *feature_len_out = l.crc_end - l.feature_off;
    return TRUE;
}
