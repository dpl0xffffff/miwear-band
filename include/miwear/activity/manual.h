#ifndef MIWEAR_ACTIVITY_MANUAL_H
#define MIWEAR_ACTIVITY_MANUAL_H

#include "miwear/app.h"

#include <json-glib/json-glib.h>

/* Mi Fitness ManualMeasureParser (dailyType 6) / band subtype MANUAL_SAMPLES (0x06). */
void miwear_apply_manual_samples(AppState *state, uint32_t file_day_ts, const uint8_t *payload, size_t payload_len);
guint miwear_apply_manual_measurement_json(AppState *state, uint32_t file_day_ts, JsonNode *decoded);
void miwear_manual_entries_reset(void);
guint miwear_manual_entries_committed(void);

#endif
