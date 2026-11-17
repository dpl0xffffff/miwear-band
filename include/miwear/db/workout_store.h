#ifndef MIWEAR_DB_WORKOUT_STORE_H
#define MIWEAR_DB_WORKOUT_STORE_H

#include "miwear/activity/workout_types.h"

gchar *miwear_workout_summary_to_json(int sport_type, const MiwearWorkoutSummary *summary);
gboolean miwear_workout_summary_from_json(const char *json,
                                          int *sport_type_out,
                                          MiwearWorkoutSummary *summary_out);

#endif
