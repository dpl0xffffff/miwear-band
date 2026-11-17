#ifndef MIWEAR_ACTIVITY_WORKOUT_RECORD_H
#define MIWEAR_ACTIVITY_WORKOUT_RECORD_H

#include "miwear/activity/workout_types.h"

gboolean miwear_parse_workout_record(const uint8_t *payload,
                                     size_t payload_len,
                                     int sport_subtype,
                                     int version,
                                     MiwearWorkoutSummary *summary);

gboolean miwear_parse_workout_gps(const uint8_t *payload,
                                  size_t payload_len,
                                  int sport_subtype,
                                  int version,
                                  MiwearWorkoutSummary *summary);

#endif
