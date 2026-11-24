#ifndef SALUS_SPP_WIRE_INTERNAL_H
#define SALUS_SPP_WIRE_INTERNAL_H

#include "salus/app.h"

/* Used by fitness_sync.c (Mi Fitness orchestration). */
void salus_wire_send_device_bootstrap(SalusApp *state);
void salus_wire_begin_file_sync(SalusApp *state, const char *today_label);
void salus_wire_resync_file_lists(SalusApp *state, const char *today_label);

struct Xiaomi__Command;
gboolean salus_wire_send_xiaomi_command(SalusApp *state, struct Xiaomi__Command *cmd, const char *label);

/* type=2 system: subtype 17 find phone ack, 18 find band/watch (GB XiaomiSystemService). */
gboolean salus_wire_send_find_device(SalusApp *state, uint32_t subtype, uint32_t find_device, const char *label);
gboolean salus_wire_find_band(SalusApp *state, gboolean start);

#endif
