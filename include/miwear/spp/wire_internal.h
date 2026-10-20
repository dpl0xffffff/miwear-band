#ifndef MIWEAR_SPP_WIRE_INTERNAL_H
#define MIWEAR_SPP_WIRE_INTERNAL_H

#include "miwear/app.h"

/* Used by fitness_sync.c (Mi Fitness orchestration). */
void miwear_wire_send_device_bootstrap(AppState *state);
void miwear_wire_begin_file_sync(AppState *state, const char *today_label);
void miwear_wire_resync_file_lists(AppState *state, const char *today_label);

struct Xiaomi__Command;
gboolean miwear_wire_send_xiaomi_command(AppState *state, struct Xiaomi__Command *cmd, const char *label);

/* type=2 system: subtype 17 find phone ack, 18 find band/watch (GB XiaomiSystemService). */
gboolean miwear_wire_send_find_device(AppState *state, uint32_t subtype, uint32_t find_device, const char *label);
gboolean miwear_wire_find_band(AppState *state, gboolean start);

#endif
