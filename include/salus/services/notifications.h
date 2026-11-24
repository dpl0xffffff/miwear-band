#ifndef SALUS_NOTIFICATIONS_H
#define SALUS_NOTIFICATIONS_H

#include "salus/app.h"


/* Starts capturing desktop notifications when forwarding is enabled. */
void salus_notifications_start(SalusApp *state);
void salus_notifications_on_disconnect(SalusApp *state);

void salus_notifications_set_enabled(SalusApp *state, gboolean enabled);
void salus_notifications_send_test(SalusApp *state);

void salus_notifications_shutdown(SalusApp *state);

#endif
