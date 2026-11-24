#ifndef SALUS_SPP_WIRE_H
#define SALUS_SPP_WIRE_H

#include "salus/app.h"

void salus_log_fetch_transition(SalusApp *state, const char *event, const uint8_t *file_id7);

void disconnect_socket(SalusApp *state);
void reset_activity_fetch_state(SalusApp *state);
gboolean send_activity_fetch_ack(SalusApp *state, const uint8_t *ids, size_t ids_len);
void trigger_next_activity_fetch(SalusApp *state);

#endif
