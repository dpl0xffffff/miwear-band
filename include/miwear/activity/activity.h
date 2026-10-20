#ifndef MIWEAR_ACTIVITY_H
#define MIWEAR_ACTIVITY_H

#include "miwear/app.h"

void handle_activity_payload(AppState *state, const uint8_t *payload, size_t payload_len);
const char *activity_subtype_name(int type, int subtype);

/* Maps Gadgetbridge / wire raw sleep codes to UI hypnogram buckets (2=deep … 5=awake). */
int miwear_normalize_sleep_stage(int raw_stage);

#endif
