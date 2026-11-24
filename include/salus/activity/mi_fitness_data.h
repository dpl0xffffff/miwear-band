#ifndef SALUS_ACTIVITY_MI_FITNESS_DATA_H
#define SALUS_ACTIVITY_MI_FITNESS_DATA_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct SalusFitnessDataId {
    uint32_t timestamp_sec;
    int8_t tz_15min;
    uint8_t version;
    uint8_t sport_type;
    uint8_t daily_type;
    uint8_t file_type;
    uint8_t data_type; /* 0 daily, 1 sport */
} SalusFitnessDataId;

typedef struct SalusFitnessBinarySlice {
    const uint8_t *data_valid;
    size_t data_valid_len;
    const uint8_t *body;
    size_t body_len;
    bool invalid_version;
} SalusFitnessBinarySlice;

/* Band wire subtype (activity file flags) → Mi Fitness FitnessDataId.dailyType. */
int mi_fitness_apk_daily_type(int wire_daily_subtype);

bool mi_fitness_data_id_from_payload(const uint8_t *payload, size_t payload_len, int file_type, SalusFitnessDataId *out);

bool mi_fitness_split_native_payload(const uint8_t *payload,
                                     size_t payload_len,
                                     const SalusFitnessDataId *id,
                                     SalusFitnessBinarySlice *out);

#endif
