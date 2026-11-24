#ifndef MIWEAR_ACTIVITY_MI_FITNESS_FULL_DAY_SLEEP_H
#define MIWEAR_ACTIVITY_MI_FITNESS_FULL_DAY_SLEEP_H

#include <glib.h>
#include <stddef.h>
#include <stdint.h>

/* full_day_sleep report v5/v6 validity bitmap (field 0 = MSB). */
gboolean miwear_full_day_sleep_valid_bit(const uint8_t *vf, size_t vf_len, int field_index);

/* One per-sample series of the report (heart rate, SpO2, HRV, snore). */
typedef struct {
    gboolean present;
    uint16_t interval_sec;
    uint16_t count;
    uint32_t first_time; /* 0 when count == 0 */
    size_t values_off;   /* byte offset of the first value in the payload */
} MiwearFullDaySleepSeries;

/*
 * Field layout of a full_day_sleep report, following Mi Fitness's
 * full_day_sleep_report_v5/v6 schemas. v6 adds eleven HRV summary fields (validity bits 8-18)
 * and an HRV series before the snore series, so the HR/SpO2/snore bits move from 8/9/10 to 19/20/22.
 */
typedef struct {
    uint8_t sleep_full;
    uint32_t start_time;
    uint32_t end_time;
    uint32_t go_bed_time;
    uint32_t live_bed_time;
    MiwearFullDaySleepSeries heart_rate;  /* uint8 values */
    MiwearFullDaySleepSeries spo2;        /* uint8 values */
    MiwearFullDaySleepSeries hrv;         /* uint16 values, v6 only */
    MiwearFullDaySleepSeries snore;       /* uint32 values */
    size_t feature_off;                   /* start of featureData (== crc_end when empty) */
    size_t crc_end;
} MiwearFullDaySleepLayout;

/* FALSE for an unsupported version, a too-short payload or a series running past the end. */
gboolean miwear_full_day_sleep_layout(const uint8_t *payload,
                                      size_t payload_len,
                                      int version,
                                      MiwearFullDaySleepLayout *out);

/* Byte offset after fixed fields + series, before featureData tail (0 when unparsable). */
size_t miwear_full_day_sleep_structured_end(const uint8_t *payload, size_t payload_len, int version);

gboolean mi_fitness_full_day_sleep_bed_wake(const uint8_t *payload,
                                            size_t payload_len,
                                            int version,
                                            uint32_t *bed_sec_out,
                                            uint32_t *wake_sec_out);

gboolean mi_fitness_full_day_sleep_feature_tail(const uint8_t *payload,
                                                size_t payload_len,
                                                int version,
                                                const uint8_t **feature_out,
                                                size_t *feature_len_out);

#endif
