#ifndef MIWEAR_ACTIVITY_MI_FITNESS_ONEDEN_H
#define MIWEAR_ACTIVITY_MI_FITNESS_ONEDEN_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Mirrors FitnessDataBaseParser.OneDimenDataType (Mi Fitness APK). */
typedef struct MiFitnessOneDimenField {
    int16_t type;
    uint8_t byte_count;
    uint8_t support_version;
    uint8_t is_float;
    uint8_t is_unsigned;
    int16_t counts_type; /* -1 if none */
    uint8_t has_depend;
    /* When has_depend: 1=courseIdExistType (field 205 value 1), 2=lactateTypeExistType (205 values 2,3). */
    uint8_t depend_profile;
} MiFitnessOneDimenField;

#define MI_FITNESS_DEPEND_COURSE 1
#define MI_FITNESS_DEPEND_LACTATE 2

typedef struct MiFitnessFieldValue {
    int type;
    bool valid;
    bool is_float;
    int64_t i64;
    float f32;
} MiFitnessFieldValue;

#define MI_FITNESS_FIELD_VALUE_MAX 128

extern const MiFitnessOneDimenField mi_fitness_outdoor_step_report_fields[];
extern const size_t mi_fitness_outdoor_step_report_field_count;

extern const MiFitnessOneDimenField mi_fitness_outdoor_sport_report_fields[];
extern const size_t mi_fitness_outdoor_sport_report_field_count;

extern const MiFitnessOneDimenField mi_fitness_abnormal_record_fields[];
extern const size_t mi_fitness_abnormal_record_field_count;

extern const MiFitnessOneDimenField mi_fitness_weight_record_fields[];
extern const size_t mi_fitness_weight_record_field_count;

extern const MiFitnessOneDimenField mi_fitness_sport_gps_fields[];
extern const size_t mi_fitness_sport_gps_field_count;

extern const MiFitnessOneDimenField mi_fitness_ecg_measure_fields[];
extern const size_t mi_fitness_ecg_measure_field_count;

extern const MiFitnessOneDimenField mi_fitness_temperature_measure_fields[];
extern const size_t mi_fitness_temperature_measure_field_count;

bool mi_fitness_oneden_parse_report(const MiFitnessOneDimenField *fields,
                                    size_t field_count,
                                    int data_version,
                                    const uint8_t *data_valid,
                                    size_t data_valid_len,
                                    const uint8_t *body,
                                    size_t body_len,
                                    MiFitnessFieldValue *out_values,
                                    size_t *out_count);

size_t mi_fitness_oneden_one_record_body_len(const MiFitnessOneDimenField *fields,
                                             size_t field_count,
                                             int data_version);

bool mi_fitness_oneden_parse_one_record(const MiFitnessOneDimenField *fields,
                                        size_t field_count,
                                        int data_version,
                                        const uint8_t *data_valid,
                                        size_t data_valid_len,
                                        const uint8_t *body,
                                        size_t body_len,
                                        size_t *inout_off,
                                        MiFitnessFieldValue *out_values,
                                        size_t *out_count);

size_t mi_fitness_oneden_parse_loop_records(const MiFitnessOneDimenField *fields,
                                            size_t field_count,
                                            int data_version,
                                            const uint8_t *data_valid,
                                            size_t data_valid_len,
                                            const uint8_t *body,
                                            size_t body_len);

#endif
