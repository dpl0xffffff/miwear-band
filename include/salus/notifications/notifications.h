#ifndef SALUS_NOTIFICATIONS_H
#define SALUS_NOTIFICATIONS_H

#include "salus/app.h"

struct Xiaomi__Command;

void salus_notifications_on_connect(SalusApp *state);
void salus_notifications_on_disconnect(SalusApp *state);
void salus_notifications_handle_command(SalusApp *state, const struct Xiaomi__Command *cmd);

void salus_notifications_set_enabled(SalusApp *state, gboolean enabled);
void salus_notifications_send_test(SalusApp *state);

void salus_notifications_shutdown(SalusApp *state);

#endif
