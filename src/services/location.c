#include "salus/services/location.h"

#include <geoclue.h>
#include <math.h>

#define GEOCLUE_DESKTOP_ID "io.salus.Salus"
#define MAX_ACCURACY_M 50.0

typedef struct {
    SalusApp *app;
    SalusLocationCallbacks cb;
    gpointer user_data;
    GCancellable *cancel;
    GClueSimple *simple;
    gulong location_handler;
    guint delivered;
} Tracker;

static Tracker *s_tracker; /* one active device at a time */

static gboolean fix_from_geoclue(GClueLocation *loc, SalusLocationFix *out) {
    if (loc == NULL) {
        return FALSE;
    }
    const double acc = gclue_location_get_accuracy(loc);
    if (!isfinite(acc) || acc < 0 || acc > MAX_ACCURACY_M) {
        return FALSE;
    }
    out->latitude = gclue_location_get_latitude(loc);
    out->longitude = gclue_location_get_longitude(loc);
    const double alt = gclue_location_get_altitude(loc);
    out->altitude = alt > -1e6 ? alt : NAN; /* GeoClue: -G_MAXDOUBLE when unknown */
    out->speed = gclue_location_get_speed(loc);
    out->heading = gclue_location_get_heading(loc);
    out->accuracy = acc;
    out->timestamp = g_get_real_time() / G_USEC_PER_SEC;
    GVariant *tv = gclue_location_get_timestamp(loc);
    if (tv != NULL && g_variant_is_of_type(tv, G_VARIANT_TYPE("(tt)"))) {
        guint64 sec = 0, usec = 0;
        g_variant_get(tv, "(tt)", &sec, &usec);
        if (sec > 0) {
            out->timestamp = (gint64)sec;
        }
    }
    return TRUE;
}

static void deliver(Tracker *t, GClueLocation *loc) {
    SalusLocationFix fix;
    if (fix_from_geoclue(loc, &fix)) {
        t->delivered++;
        t->cb.fix(t->app, &fix, t->user_data);
    }
}

static void on_location_changed(GClueSimple *simple, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    deliver(user_data, gclue_simple_get_location(simple));
}

static void not_available(Tracker *t, const char *why) {
    SalusApp *app = t->app;
    const SalusLocationCallbacks cb = t->cb;
    gpointer user_data = t->user_data;
    salus_location_stop(app, NULL);
    cb.ready(app, FALSE, why, user_data);
}

static void on_simple_ready(GObject *source, GAsyncResult *res, gpointer user_data) {
    (void)source;
    GError *err = NULL;
    GClueSimple *simple = gclue_simple_new_with_thresholds_finish(res, &err);
    if (g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        g_clear_error(&err);
        return; /* the tracker was already freed by stop() */
    }
    Tracker *t = user_data;
    if (simple == NULL) {
        not_available(t, err != NULL ? err->message : "GeoClue error");
        g_clear_error(&err);
        return;
    }
    t->simple = simple;
    SalusLocationFix first;
    GClueLocation *loc = gclue_simple_get_location(simple);
    if (!fix_from_geoclue(loc, &first)) {
        gchar *why = loc != NULL ? g_strdup_printf("best fix ±%.0f m", gclue_location_get_accuracy(loc))
                                 : g_strdup("no fix");
        not_available(t, why);
        g_free(why);
        return;
    }
    append_log(t->app, "Location: sharing this computer's location with the device.");
    t->cb.ready(t->app, TRUE, NULL, t->user_data);
    t->location_handler = g_signal_connect(simple, "notify::location", G_CALLBACK(on_location_changed), t);
    deliver(t, loc);
}

void salus_location_start(SalusApp *app, const SalusLocationCallbacks *cb, gpointer user_data) {
    if (s_tracker != NULL) {
        return;
    }
    if (!app->share_location_with_band) {
        cb->ready(app, FALSE, "sharing is off on the Device page", user_data);
        return;
    }
    Tracker *t = g_new0(Tracker, 1);
    t->app = app;
    t->cb = *cb;
    t->user_data = user_data;
    t->cancel = g_cancellable_new();
    s_tracker = t;
    /* Updates at most once a second, from any distance moved. */
    gclue_simple_new_with_thresholds(GEOCLUE_DESKTOP_ID, GCLUE_ACCURACY_LEVEL_EXACT, 1, 0, t->cancel,
                                     on_simple_ready, t);
}

gboolean salus_location_active(void) {
    return s_tracker != NULL && s_tracker->simple != NULL;
}

gboolean salus_location_started(void) {
    return s_tracker != NULL;
}

void salus_location_stop(SalusApp *app, const char *reason) {
    Tracker *t = s_tracker;
    if (t == NULL) {
        return;
    }
    s_tracker = NULL;
    g_cancellable_cancel(t->cancel);
    g_object_unref(t->cancel);
    if (t->simple != NULL) {
        if (t->location_handler != 0) {
            g_signal_handler_disconnect(t->simple, t->location_handler);
        }
        g_object_unref(t->simple); /* stops the GeoClue client */
    }
    if (reason != NULL && app != NULL) {
        gchar *line = g_strdup_printf("Location: stopped (%s) after %u fix(es).", reason, t->delivered);
        append_log(app, line);
        g_free(line);
    }
    g_free(t);
}
