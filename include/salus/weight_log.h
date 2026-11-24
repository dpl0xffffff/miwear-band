#ifndef SALUS_WEIGHT_LOG_H
#define SALUS_WEIGHT_LOG_H

#include "salus/app.h"

typedef struct {
    guint date_yyyymmdd;
    float kg;
} SalusWeightEntry;

void salus_weight_log_load(SalusApp *state);
gboolean salus_weight_log_add_today(SalusApp *state, float kg);
gboolean salus_weight_log_has_entries(const SalusApp *state);
float salus_weight_log_latest_kg(const SalusApp *state, guint *date_yyyymmdd_out);
float salus_weight_log_band_user_kg(const SalusApp *state);

#endif
