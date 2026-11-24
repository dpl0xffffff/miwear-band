#ifndef SALUS_ACTIVITY_MI_FITNESS_SPORT_REPORT_H
#define SALUS_ACTIVITY_MI_FITNESS_SPORT_REPORT_H

/*
 * Workout summary ("report", file type 1) decoding, as Mi Fitness does it:
 *   header = 7-byte file id + 1 byte + validity bitmap (FitnessDataValidity length),
 *   body   = the sport's field table read in order (OneDimenDataParser),
 * then every value is interpreted by its global report field id (SportReportBaseParser).
 * Tables come from tools/mi-fitness-extract; versions without a native table use the Mi Fitness
 * schema of that exact version, turned into the same kind of table.
 */

#include "salus/activity/mi_fitness_oneden.h"
#include "salus/activity/workout_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Decodes the report into field values. `source_out` (optional) names the table used. */
bool mi_fitness_sport_report_decode(const uint8_t *payload,
                                    size_t payload_len,
                                    int sport_type,
                                    int version,
                                    MiFitnessFieldValue *values,
                                    size_t *value_count,
                                    const char **source_out);

/* Fills `out` from decoded values (core fields + every other valid value in out->metrics). */
void mi_fitness_sport_report_apply(int sport_type,
                                   const MiFitnessFieldValue *values,
                                   size_t value_count,
                                   SalusWorkoutSummary *out);

/* decode + apply; FALSE when the version is unsupported or nothing usable was decoded. */
bool mi_fitness_parse_sport_report(const uint8_t *payload,
                                   size_t payload_len,
                                   int sport_type,
                                   int version,
                                   SalusWorkoutSummary *out);

#endif
