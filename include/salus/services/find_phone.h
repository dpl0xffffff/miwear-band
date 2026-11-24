#ifndef SALUS_FIND_PHONE_H
#define SALUS_FIND_PHONE_H

#include "salus/app.h"

/* The device asks the computer to ring (find_device 0) or to stop (1). */
void salus_find_phone_on_band_request(SalusApp *state, guint find_device);

/* User dismissed find-phone alert on the PC. */
void salus_find_phone_dismiss(SalusApp *state);

gboolean salus_find_phone_is_active(const SalusApp *state);

void salus_find_phone_shutdown(SalusApp *state);

#endif
