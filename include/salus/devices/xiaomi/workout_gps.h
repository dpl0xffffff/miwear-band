#pragma once

#include "salus/app.h"

struct Xiaomi__Command;

/*
 * Connected GPS: when an outdoor workout starts, the band asks the phone app for location
 * ("workout open", Health subtype 30). The app answers whether it can provide location and then
 * streams fixes (subtype 48) until the workout finishes (status, subtype 26). Fixes come from the
 * location service (GeoClue). Protocol as implemented by Gadgetbridge (XiaomiHealthService).
 */

/* Handles the workout open/status commands; ignores everything else. Main thread. */
void salus_workout_gps_handle_command(SalusApp *state, const struct Xiaomi__Command *cmd);
