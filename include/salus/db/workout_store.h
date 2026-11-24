#ifndef SALUS_DB_WORKOUT_STORE_H
#define SALUS_DB_WORKOUT_STORE_H

#include "salus/activity/workout_types.h"

gchar *salus_workout_summary_to_json(int sport_type, const SalusWorkoutSummary *summary);
gboolean salus_workout_summary_from_json(const char *json,
                                          int *sport_type_out,
                                          SalusWorkoutSummary *summary_out);

#endif
