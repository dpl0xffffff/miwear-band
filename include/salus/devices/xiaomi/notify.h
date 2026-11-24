#ifndef SALUS_DEVICES_XIAOMI_NOTIFY_H
#define SALUS_DEVICES_XIAOMI_NOTIFY_H

#include "salus/app.h"

struct Xiaomi__Command;

/* send_notification op. */
gboolean xiaomi_send_notification(SalusDevice *dev, const SalusNotification *n);
/* Asks the band for its notification preferences (screen on), as the phone app does. */
void xiaomi_request_notification_prefs(SalusApp *state);
/* Band → app notification commands (dismissals). */
void xiaomi_notifications_handle_command(SalusApp *state, const struct Xiaomi__Command *cmd);

#endif
