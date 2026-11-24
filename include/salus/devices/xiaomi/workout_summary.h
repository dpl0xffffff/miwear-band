#ifndef SALUS_ACTIVITY_WORKOUT_SUMMARY_H
#define SALUS_ACTIVITY_WORKOUT_SUMMARY_H

#include <stddef.h>
#include <stdint.h>

#include "salus/store/workout.h"
#include "salus/app.h"

gboolean salus_parse_workout_summary(const uint8_t *payload,
                                      size_t payload_len,
                                      int subtype,
                                      int version,
                                      uint32_t file_ts_sec,
                                      SalusWorkoutSummary *out);

/* Sport name for a Mi Fitness sport type id ("Workout" when unknown). */
const char *xiaomi_sport_name(int subtype);

void salus_apply_workout_file(SalusApp *state,
                               uint32_t file_ts,
                               int subtype,
                               int detail,
                               int version,
                               const uint8_t *payload,
                               size_t payload_len);

#endif
