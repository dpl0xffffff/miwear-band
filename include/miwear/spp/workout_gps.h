#pragma once

#include "miwear/app.h"

struct Xiaomi__Command;

/*
 * Connected GPS: when an outdoor workout starts, the band asks the phone app for location
 * ("workout open", Health subtype 30). The app answers whether it can provide location and then
 * streams fixes (subtype 48) until the workout finishes (status, subtype 26). Location comes
 * from GeoClue. Protocol as implemented by Gadgetbridge (XiaomiHealthService).
 */

/* Handles the workout open/status commands; ignores everything else. Main thread. */
void miwear_workout_gps_handle_command(AppState *state, const struct Xiaomi__Command *cmd);

/* Stops sending location (connection lost, or sharing turned off). */
void miwear_workout_gps_stop(AppState *state, const char *reason);
