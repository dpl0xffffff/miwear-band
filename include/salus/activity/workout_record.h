#ifndef SALUS_ACTIVITY_WORKOUT_RECORD_H
#define SALUS_ACTIVITY_WORKOUT_RECORD_H

#include "salus/activity/workout_types.h"

gboolean salus_parse_workout_record(const uint8_t *payload,
                                     size_t payload_len,
                                     int sport_subtype,
                                     int version,
                                     SalusWorkoutSummary *summary);

gboolean salus_parse_workout_gps(const uint8_t *payload,
                                  size_t payload_len,
                                  int sport_subtype,
                                  int version,
                                  SalusWorkoutSummary *summary);

#endif
