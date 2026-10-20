#ifndef MIWEAR_ACTIVITY_MI_FITNESS_DISPATCH_H
#define MIWEAR_ACTIVITY_MI_FITNESS_DISPATCH_H

#include "miwear/app.h"
#include <stddef.h>
#include <stdint.h>

const char *mi_fitness_daily_parser_name(int daily_type, int file_type);
const char *mi_fitness_sport_report_parser_name(int sport_type);
const char *mi_fitness_sport_record_parser_name(int sport_type);

void mi_fitness_apply_health_payload(AppState *state,
                                       uint32_t ts,
                                       int8_t tz,
                                       int daily_type,
                                       int detail,
                                       int version,
                                       const uint8_t *payload,
                                       size_t payload_len);

#endif
