#ifndef MIWEAR_SPP_WIRE_H
#define MIWEAR_SPP_WIRE_H

#include "miwear/app.h"

void miwear_log_fetch_transition(AppState *state, const char *event, const uint8_t *file_id7);

void disconnect_socket(AppState *state);
void reset_activity_fetch_state(AppState *state);
void start_connect_for_address(AppState *state, const gchar *raw_address);
gboolean send_activity_fetch_ack(AppState *state, const uint8_t *ids, size_t ids_len);
void trigger_next_activity_fetch(AppState *state);
gboolean parse_mac(const gchar *input, bdaddr_t *out_bdaddr, gchar **normalized);
gpointer connect_worker_main(gpointer user_data);

#endif
