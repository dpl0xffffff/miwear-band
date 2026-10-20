#include "miwear/activity/mi_fitness_sport_tables.h"
#include "miwear/activity/mi_fitness_data.h"
#include "miwear/activity/mi_fitness_sport_report.h"

MiFitnessSportReportTable mi_fitness_sport_report_table(int sport_type) {
    MiFitnessSportReportTable t = {0};
    switch (sport_type) {
        case 22:
            t.fields = mi_fitness_outdoor_step_report_fields;
            t.field_count = mi_fitness_outdoor_step_report_field_count;
            t.shares_outdoor_step_layout = false;
            return t;
        case 1:
        case 2:
        case 4:
        case 5:
        case 15:
            t.fields = mi_fitness_outdoor_sport_report_fields;
            t.field_count = mi_fitness_outdoor_sport_report_field_count;
            t.shares_outdoor_step_layout = false;
            return t;
        default:
            return t;
    }
}

bool mi_fitness_parse_sport_report(const uint8_t *payload,
                                   size_t payload_len,
                                   int sport_type,
                                   int version,
                                   MiwearWorkoutSummary *out) {
    if (sport_type == 22) {
        return mi_fitness_parse_outdoor_step_report(payload, payload_len, version, out);
    }

    MiFitnessSportReportTable tab = mi_fitness_sport_report_table(sport_type);
    if (tab.fields == NULL || tab.field_count == 0) {
        return false;
    }

    const size_t wire_len = payload_len >= 4 ? payload_len - 4 : payload_len;
    MiwearFitnessDataId id;
    if (!mi_fitness_data_id_from_payload(payload, wire_len, 1, &id)) {
        return false;
    }
    id.version = (uint8_t)version;
    id.sport_type = (uint8_t)sport_type;

    MiwearFitnessBinarySlice slice;
    if (!mi_fitness_split_native_payload(payload, wire_len, &id, &slice)) {
        return false;
    }

    MiFitnessFieldValue values[MI_FITNESS_FIELD_VALUE_MAX];
    size_t value_count = 0;
    if (!mi_fitness_oneden_parse_report(tab.fields,
                                        tab.field_count,
                                        version,
                                        slice.data_valid,
                                        slice.data_valid_len,
                                        slice.body,
                                        slice.body_len,
                                        values,
                                        &value_count)) {
        return false;
    }

    out->sport_subtype = sport_type;
    out->version = version;
    if (out->sport_name == NULL) {
        out->sport_name = g_strdup(miwear_sport_subtype_name(sport_type));
    }
    return mi_fitness_apply_sport_report_values(sport_type, values, value_count, out);
}
