#include "miwear/activity/mi_fitness_sport_report.h"
#include "miwear/activity/mi_fitness_data.h"

static const MiFitnessFieldValue *find_field(const MiFitnessFieldValue *values, size_t count, int type) {
    for (size_t i = 0; i < count; i++) {
        if (values[i].type == type && values[i].valid) {
            return &values[i];
        }
    }
    return NULL;
}

bool mi_fitness_apply_sport_report_values(int sport_type,
                                          const MiFitnessFieldValue *values,
                                          size_t value_count,
                                          MiwearWorkoutSummary *out) {
    if (values == NULL || out == NULL) {
        return false;
    }
    (void)sport_type;

    const MiFitnessFieldValue *start = find_field(values, value_count, 1);
    const MiFitnessFieldValue *end = find_field(values, value_count, 2);
    const MiFitnessFieldValue *duration = find_field(values, value_count, 3);
    const MiFitnessFieldValue *distance = find_field(values, value_count, 5);
    const MiFitnessFieldValue *cal_a = find_field(values, value_count, 6);
    const MiFitnessFieldValue *cal_b = find_field(values, value_count, 7);
    const MiFitnessFieldValue *hr_avg = find_field(values, value_count, 16);
    const MiFitnessFieldValue *hr_max = find_field(values, value_count, 17);
    const MiFitnessFieldValue *hr_min = find_field(values, value_count, 18);

    if (start != NULL) {
        out->start_sec = (uint32_t)start->i64;
    }
    if (end != NULL) {
        out->end_sec = (uint32_t)end->i64;
    }
    if (duration != NULL) {
        out->duration_sec = (int)duration->i64;
    }
    if (distance != NULL) {
        out->distance_m = (int)distance->i64;
    }
    if (cal_b != NULL) {
        out->calories = (int)cal_b->i64;
    } else if (cal_a != NULL) {
        out->calories = (int)cal_a->i64;
    }
    if (hr_avg != NULL) {
        out->hr_avg = (int)hr_avg->i64;
    }
    if (hr_max != NULL) {
        out->hr_max = (int)hr_max->i64;
    }
    if (hr_min != NULL) {
        out->hr_min = (int)hr_min->i64;
    }

    return out->duration_sec > 0 || out->distance_m > 0 || out->calories > 0;
}

bool mi_fitness_parse_outdoor_step_report(const uint8_t *payload,
                                          size_t payload_len,
                                          int version,
                                          MiwearWorkoutSummary *out) {
    if (payload == NULL || out == NULL || payload_len < 13) {
        return false;
    }

    const size_t wire_len = payload_len >= 4 ? payload_len - 4 : payload_len;
    MiwearFitnessDataId id;
    if (!mi_fitness_data_id_from_payload(payload, wire_len, 1, &id)) {
        return false;
    }
    id.version = (uint8_t)version;

    MiwearFitnessBinarySlice slice;
    if (!mi_fitness_split_native_payload(payload, wire_len, &id, &slice)) {
        return false;
    }

    MiFitnessFieldValue values[MI_FITNESS_FIELD_VALUE_MAX];
    size_t value_count = 0;
    if (!mi_fitness_oneden_parse_report(mi_fitness_outdoor_step_report_fields,
                                        mi_fitness_outdoor_step_report_field_count,
                                        version,
                                        slice.data_valid,
                                        slice.data_valid_len,
                                        slice.body,
                                        slice.body_len,
                                        values,
                                        &value_count)) {
        return false;
    }

    out->sport_subtype = id.sport_type > 0 ? id.sport_type : 22;
    out->version = version;
    if (out->sport_name == NULL) {
        out->sport_name = g_strdup(miwear_sport_subtype_name(out->sport_subtype));
    }
    return mi_fitness_apply_sport_report_values(id.sport_type, values, value_count, out);
}
