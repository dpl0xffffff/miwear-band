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
    const MiFitnessFieldValue *duration_total = find_field(values, value_count, 4);
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
    } else if (duration_total != NULL) {
        out->duration_sec = (int)duration_total->i64;
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

    const MiFitnessFieldValue *pace_avg = find_field(values, value_count, 10);
    const MiFitnessFieldValue *speed_avg = find_field(values, value_count, 11);
    const MiFitnessFieldValue *steps = find_field(values, value_count, 13);
    const MiFitnessFieldValue *cadence_avg = find_field(values, value_count, 14);
    const MiFitnessFieldValue *cadence_max = find_field(values, value_count, 15);
    const MiFitnessFieldValue *training_load = find_field(values, value_count, 29);
    if (training_load == NULL) {
        training_load = find_field(values, value_count, 93);
    }
    if (training_load == NULL) {
        training_load = find_field(values, value_count, 30);
    }
    const MiFitnessFieldValue *recovery = find_field(values, value_count, 96);
    const MiFitnessFieldValue *te = find_field(values, value_count, 99);
    const MiFitnessFieldValue *vitality = find_field(values, value_count, 92);
    if (vitality == NULL) {
        vitality = find_field(values, value_count, 95);
    }

    if (pace_avg != NULL) {
        out->walk_avg_pace_sec_per_km = (int)pace_avg->i64;
    }
    if (speed_avg != NULL) {
        out->walk_avg_speed_kmh =
            speed_avg->is_float ? speed_avg->f32 : (float)speed_avg->i64;
    }
    if (steps != NULL) {
        out->walk_steps = (int)steps->i64;
    }
    if (cadence_avg != NULL) {
        out->walk_avg_cadence_spm = (int)cadence_avg->i64;
    }
    if (cadence_max != NULL) {
        out->walk_max_cadence_spm = (int)cadence_max->i64;
    }
    if (training_load != NULL) {
        out->walk_training_load = (int)training_load->i64;
    }
    if (recovery != NULL) {
        const int r = (int)recovery->i64;
        out->walk_recovery_min = (r > 0 && r <= 24) ? r * 60 : r;
    }
    if (te != NULL) {
        out->walk_te_aerobic = te->is_float ? te->f32 : (float)te->i64;
    }
    if (vitality != NULL) {
        out->walk_vitality_points = (int)vitality->i64;
    }

    const MiFitnessFieldValue *z1 = find_field(values, value_count, 19);
    const MiFitnessFieldValue *z2 = find_field(values, value_count, 20);
    const MiFitnessFieldValue *z3 = find_field(values, value_count, 21);
    const MiFitnessFieldValue *z4 = find_field(values, value_count, 22);
    const MiFitnessFieldValue *z5 = find_field(values, value_count, 23);
    if (z1 != NULL) {
        out->hr_zones.zone1_sec = (int)z1->i64;
    }
    if (z2 != NULL) {
        out->hr_zones.zone2_sec = (int)z2->i64;
    }
    if (z3 != NULL) {
        out->hr_zones.zone3_sec = (int)z3->i64;
    }
    if (z4 != NULL) {
        out->hr_zones.zone4_sec = (int)z4->i64;
    }
    if (z5 != NULL) {
        out->hr_zones.zone5_sec = (int)z5->i64;
    }
    const MiFitnessFieldValue *zl1 = find_field(values, value_count, 137);
    const MiFitnessFieldValue *zl2 = find_field(values, value_count, 138);
    const MiFitnessFieldValue *zl3 = find_field(values, value_count, 139);
    const MiFitnessFieldValue *zl4 = find_field(values, value_count, 140);
    const MiFitnessFieldValue *zl5 = find_field(values, value_count, 141);
    if (zl1 != NULL) {
        out->hr_zones.zone1_lower_bpm = (int)zl1->i64;
    }
    if (zl2 != NULL) {
        out->hr_zones.zone2_lower_bpm = (int)zl2->i64;
    }
    if (zl3 != NULL) {
        out->hr_zones.zone3_lower_bpm = (int)zl3->i64;
    }
    if (zl4 != NULL) {
        out->hr_zones.zone4_lower_bpm = (int)zl4->i64;
    }
    if (zl5 != NULL) {
        out->hr_zones.zone5_lower_bpm = (int)zl5->i64;
    }

    if (out->walk_avg_pace_sec_per_km <= 0 && out->distance_m > 0 && out->duration_sec > 0) {
        out->walk_avg_pace_sec_per_km = (int)((uint64_t)out->duration_sec * 1000u / (uint32_t)out->distance_m);
    }
    if (out->duration_sec > 0 || out->distance_m > 0 || out->calories > 0 || out->walk_steps > 0) {
        out->has_walk_detail = TRUE;
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
