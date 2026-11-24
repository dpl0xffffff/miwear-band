#ifndef SALUS_SERVICES_LOCATION_H
#define SALUS_SERVICES_LOCATION_H

/*
 * This computer's location (GeoClue), for devices that record outdoor workouts with the phone's
 * GPS ("connected GPS"). Only fixes accurate to 50 m are used: Wi-Fi/IP location on a computer
 * without GPS would pull the workout track off course. Respects [location] share_with_band.
 */

#include "salus/app.h"

typedef struct {
    double latitude;
    double longitude;
    double altitude;  /* NAN when unknown */
    double speed;     /* m/s, < 0 when unknown */
    double heading;   /* degrees, < 0 when unknown */
    double accuracy;  /* metres */
    gint64 timestamp; /* unix seconds */
} SalusLocationFix;

typedef struct {
    /* Once, after start: whether fixes will follow, and if not why (for the log). */
    void (*ready)(SalusApp *app, gboolean available, const char *why, gpointer user_data);
    void (*fix)(SalusApp *app, const SalusLocationFix *fix, gpointer user_data);
} SalusLocationCallbacks;

/* Starts tracking; a second start while tracking is ignored (check salus_location_active()). */
void salus_location_start(SalusApp *app, const SalusLocationCallbacks *cb, gpointer user_data);
/* TRUE while fixes are being delivered (after ready(available=TRUE)). */
gboolean salus_location_active(void);
/* TRUE between start and stop, including while GeoClue is still starting. */
gboolean salus_location_started(void);
void salus_location_stop(SalusApp *app, const char *reason);

#endif
