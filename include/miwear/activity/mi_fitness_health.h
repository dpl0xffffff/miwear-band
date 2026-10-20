#ifndef MIWEAR_ACTIVITY_MI_FITNESS_HEALTH_H
#define MIWEAR_ACTIVITY_MI_FITNESS_HEALTH_H

#include "miwear/app.h"
#include <json-glib/json-glib.h>
#include <stddef.h>
#include <stdint.h>

void mi_fitness_apply_schema_health(AppState *state,
                                    uint32_t ts,
                                    int apk_daily_type,
                                    int detail,
                                    JsonNode *decoded,
                                    const uint8_t *payload,
                                    size_t payload_len);

gboolean mi_fitness_apply_native_health(AppState *state,
                                        uint32_t ts,
                                        int wire_daily_type,
                                        int detail,
                                        int version,
                                        const uint8_t *payload,
                                        size_t payload_len);

#endif
