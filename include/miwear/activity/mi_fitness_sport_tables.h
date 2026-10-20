#ifndef MIWEAR_ACTIVITY_MI_FITNESS_SPORT_TABLES_H
#define MIWEAR_ACTIVITY_MI_FITNESS_SPORT_TABLES_H

#include "miwear/activity/mi_fitness_oneden.h"
#include "miwear/activity/workout_types.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct MiFitnessSportReportTable {
    const MiFitnessOneDimenField *fields;
    size_t field_count;
    bool shares_outdoor_step_layout;
} MiFitnessSportReportTable;

MiFitnessSportReportTable mi_fitness_sport_report_table(int sport_type);

bool mi_fitness_parse_sport_report(const uint8_t *payload,
                                   size_t payload_len,
                                   int sport_type,
                                   int version,
                                   MiwearWorkoutSummary *out);

#endif
