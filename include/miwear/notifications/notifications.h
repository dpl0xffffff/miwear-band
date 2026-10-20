#ifndef MIWEAR_NOTIFICATIONS_H
#define MIWEAR_NOTIFICATIONS_H

#include "miwear/app.h"

struct Xiaomi__Command;

void miwear_notifications_on_connect(AppState *state);
void miwear_notifications_on_disconnect(AppState *state);
void miwear_notifications_handle_command(AppState *state, const struct Xiaomi__Command *cmd);

void miwear_notifications_set_enabled(AppState *state, gboolean enabled);
void miwear_notifications_send_test(AppState *state);

void miwear_notifications_shutdown(AppState *state);

#endif
