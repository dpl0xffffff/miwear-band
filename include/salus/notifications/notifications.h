#ifndef SALUS_NOTIFICATIONS_H
#define SALUS_NOTIFICATIONS_H

#include "salus/app.h"

struct Xiaomi__Command;

void salus_notifications_on_connect(AppState *state);
void salus_notifications_on_disconnect(AppState *state);
void salus_notifications_handle_command(AppState *state, const struct Xiaomi__Command *cmd);

void salus_notifications_set_enabled(AppState *state, gboolean enabled);
void salus_notifications_send_test(AppState *state);

void salus_notifications_shutdown(AppState *state);

#endif
