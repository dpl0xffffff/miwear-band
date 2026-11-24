#ifndef SALUS_WEIGHT_LOG_H
#define SALUS_WEIGHT_LOG_H

#include "salus/app.h"

typedef struct {
    guint date_yyyymmdd;
    float kg;
} SalusWeightEntry;

void salus_weight_log_load(AppState *state);
gboolean salus_weight_log_add_today(AppState *state, float kg);
gboolean salus_weight_log_has_entries(const AppState *state);
float salus_weight_log_latest_kg(const AppState *state, guint *date_yyyymmdd_out);
float salus_weight_log_band_user_kg(const AppState *state);

#endif
