#ifndef MIWEAR_ACTIVITY_MI_FITNESS_SPORT_REPORT_H
#define MIWEAR_ACTIVITY_MI_FITNESS_SPORT_REPORT_H

#include "miwear/activity/mi_fitness_oneden.h"
#include "miwear/activity/workout_types.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

bool mi_fitness_apply_sport_report_values(int sport_type,
                                          const MiFitnessFieldValue *values,
                                          size_t value_count,
                                          MiwearWorkoutSummary *out);

bool mi_fitness_parse_outdoor_step_report(const uint8_t *payload,
                                          size_t payload_len,
                                          int version,
                                          MiwearWorkoutSummary *out);

#endif
