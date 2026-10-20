#ifndef MIWEAR_ACTIVITY_MI_FITNESS_FULL_DAY_SLEEP_H
#define MIWEAR_ACTIVITY_MI_FITNESS_FULL_DAY_SLEEP_H

#include <glib.h>
#include <stddef.h>
#include <stdint.h>

/* full_day_sleep report v5/v6 validity bitmap (field 0 = MSB). */
gboolean miwear_full_day_sleep_valid_bit(const uint8_t *vf, size_t vf_len, int field_index);

/* Byte offset after fixed fields + HR/SpO2/snore series, before featureData tail. */
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
