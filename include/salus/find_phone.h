#ifndef SALUS_FIND_PHONE_H
#define SALUS_FIND_PHONE_H

#include "salus/app.h"

/* Band → phone (Mi Fit / GB CMD_FIND_PHONE): findDevice 0 = start ringing, 1 = stop. */
void salus_find_phone_on_band_request(AppState *state, guint find_device);

/* User dismissed find-phone alert on the PC. */
void salus_find_phone_dismiss(AppState *state);

gboolean salus_find_phone_is_active(const AppState *state);

void salus_find_phone_shutdown(AppState *state);

#endif
