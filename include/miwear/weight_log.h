#ifndef MIWEAR_WEIGHT_LOG_H
#define MIWEAR_WEIGHT_LOG_H

#include "miwear/app.h"

typedef struct {
    guint date_yyyymmdd;
    float kg;
} MiwearWeightEntry;

void miwear_weight_log_load(AppState *state);
gboolean miwear_weight_log_add_today(AppState *state, float kg);
gboolean miwear_weight_log_has_entries(const AppState *state);
float miwear_weight_log_latest_kg(const AppState *state, guint *date_yyyymmdd_out);
float miwear_weight_log_band_user_kg(const AppState *state);

#endif
