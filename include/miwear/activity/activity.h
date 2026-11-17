#ifndef MIWEAR_ACTIVITY_H
#define MIWEAR_ACTIVITY_H

#include "miwear/app.h"

void handle_activity_payload(AppState *state, const uint8_t *payload, size_t payload_len);
const char *activity_subtype_name(int type, int subtype);

#include "miwear/sleep/mi_sleep_packets.h"

#endif
