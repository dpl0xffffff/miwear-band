#ifndef MIWEAR_ACTIVITY_WORKOUT_SUMMARY_H
#define MIWEAR_ACTIVITY_WORKOUT_SUMMARY_H

#include <stddef.h>
#include <stdint.h>

#include "miwear/activity/workout_types.h"
#include "miwear/app.h"

gboolean miwear_parse_workout_summary(const uint8_t *payload,
                                      size_t payload_len,
                                      int subtype,
                                      int version,
                                      uint32_t file_ts_sec,
                                      MiwearWorkoutSummary *out);

void miwear_apply_workout_file(AppState *state,
                               uint32_t file_ts,
                               int subtype,
                               int detail,
                               int version,
                               const uint8_t *payload,
                               size_t payload_len);

#endif
