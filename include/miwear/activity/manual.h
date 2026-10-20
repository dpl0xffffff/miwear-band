#ifndef MIWEAR_ACTIVITY_MANUAL_H
#define MIWEAR_ACTIVITY_MANUAL_H

#include "miwear/app.h"

/* Mi Fitness ManualMeasureParser (dailyType 6) / band subtype MANUAL_SAMPLES (0x06). */
void miwear_apply_manual_samples(AppState *state, uint32_t file_day_ts, const uint8_t *payload, size_t payload_len);

#endif
