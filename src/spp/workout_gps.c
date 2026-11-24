#include "miwear/spp/workout_gps.h"

#include "miwear/spp/wire_internal.h"

#include <geoclue.h>
#include <math.h>

#include "xiaomi.pb-c.h"

#define HEALTH_CMD_TYPE 8u
#define CMD_WORKOUT_WATCH_STATUS 26u
#define CMD_WORKOUT_WATCH_OPEN 30u
#define CMD_WORKOUT_LOCATION 48u

#define WORKOUT_STATUS_FINISHED 3u

#define GEOCLUE_DESKTOP_ID "io.salus.Salus"

/* Coarser fixes (Wi-Fi/IP location on a computer without GPS) would pull the band's track off
 * course, so they are never sent. */
#define MAX_ACCURACY_M 50.0

typedef struct {
    AppState *state;
    GCancellable *cancel;
    GClueSimple *simple;
    gulong location_handler;
    guint sent;
} WorkoutGps;

static WorkoutGps *s_gps; /* one band connection at a time */

static void send_open_reply(AppState *state, gboolean have_location) {
    /* Gadgetbridge: 0/2/2 when the phone provides location, 3/2/10 when it has no location
     * permission at all (the band then uses its own GPS only). */
    Xiaomi__WorkoutOpenReply reply = XIAOMI__WORKOUT_OPEN_REPLY__INIT;
    reply.has_unknown1 = reply.has_unknown2 = reply.has_unknown3 = TRUE;
    reply.unknown1 = have_location ? 0 : 3;
    reply.unknown2 = 2;
    reply.unknown3 = have_location ? 2 : 10;
    Xiaomi__Health health = XIAOMI__HEALTH__INIT;
    health.workoutopenreply = &reply;
    Xiaomi__Command cmd = XIAOMI__COMMAND__INIT;
    cmd.type = HEALTH_CMD_TYPE;
    cmd.has_subtype = TRUE;
    cmd.subtype = CMD_WORKOUT_WATCH_OPEN;
    cmd.health = &health;
    (void)miwear_wire_send_xiaomi_command(state, &cmd,
                                          have_location ? "Workout GPS: replied with computer location available"
                                                        : "Workout GPS: replied without computer location");
}

static gboolean location_usable(GClueLocation *loc) {
    if (loc == NULL) {
        return FALSE;
    }
    const double acc = gclue_location_get_accuracy(loc);
    return isfinite(acc) && acc >= 0 && acc <= MAX_ACCURACY_M;
}

/* No usable location. By default the app stays silent, as before connected GPS existed: the
 * "no permission" reply makes the band show "grant location permission on your phone". */
static void reply_unavailable(AppState *state, const char *why) {
    const char *mode = state->location_unavailable_reply != NULL ? state->location_unavailable_reply : "none";
    gchar *line = g_strdup_printf("Workout GPS: no usable location on this computer (%s); reply_when_unavailable=%s.",
                                  why, mode);
    append_log(state, line);
    g_free(line);
    if (g_strcmp0(mode, "denied") == 0) {
        send_open_reply(state, FALSE);
    } else if (g_strcmp0(mode, "ok") == 0) {
        send_open_reply(state, TRUE); /* experiment: claim location, send none */
    }
}

static void send_location(WorkoutGps *g, GClueLocation *loc) {
    if (!location_usable(loc)) {
        return;
    }
    Xiaomi__WorkoutLocation wl = XIAOMI__WORKOUT_LOCATION__INIT;
    wl.has_unknown1 = TRUE;
    wl.unknown1 = 10; /* Gadgetbridge: "number of satellites", always 10 */
    gint64 ts = g_get_real_time() / G_USEC_PER_SEC;
    GVariant *tv = gclue_location_get_timestamp(loc);
    if (tv != NULL && g_variant_is_of_type(tv, G_VARIANT_TYPE("(tt)"))) {
        guint64 sec = 0, usec = 0;
        g_variant_get(tv, "(tt)", &sec, &usec);
        if (sec > 0) {
            ts = (gint64)sec;
        }
    }
    wl.has_timestamp = TRUE;
    wl.timestamp = (uint32_t)ts;
    wl.has_latitude = wl.has_longitude = TRUE;
    wl.latitude = gclue_location_get_latitude(loc);
    wl.longitude = gclue_location_get_longitude(loc);
    const double alt = gclue_location_get_altitude(loc);
    if (alt > -1e6) { /* GeoClue: -G_MAXDOUBLE when unknown */
        wl.has_altitude = TRUE;
        wl.altitude = alt;
    }
    const double speed = gclue_location_get_speed(loc);
    if (speed >= 0) { /* -1 when unknown */
        wl.has_speed = TRUE;
        wl.speed = (float)speed;
    }
    const double heading = gclue_location_get_heading(loc);
    if (heading >= 0) {
        wl.has_bearing = TRUE;
        wl.bearing = (float)heading;
    }
    const double acc = gclue_location_get_accuracy(loc);
    if (acc >= 0 && isfinite(acc)) {
        wl.has_horizontalaccuracy = TRUE;
        wl.horizontalaccuracy = (float)acc;
    }

    Xiaomi__Health health = XIAOMI__HEALTH__INIT;
    health.workoutlocation = &wl;
    Xiaomi__Command cmd = XIAOMI__COMMAND__INIT;
    cmd.type = HEALTH_CMD_TYPE;
    cmd.has_subtype = TRUE;
    cmd.subtype = CMD_WORKOUT_LOCATION;
    cmd.health = &health;
    const gboolean ok = miwear_wire_send_xiaomi_command(g->state, &cmd, NULL);
    /* Log the first fix and then every 30th, so a long walk doesn't flood the log. */
    if (!ok || g->sent == 0 || g->sent % 30 == 0) {
        gchar *line = g_strdup_printf("Workout GPS: %s location #%u (%.5f, %.5f ±%.0f m)",
                                      ok ? "sent" : "failed to send", g->sent + 1, wl.latitude, wl.longitude,
                                      wl.has_horizontalaccuracy ? wl.horizontalaccuracy : -1.0);
        append_log(g->state, line);
        g_free(line);
    }
    if (ok) {
        g->sent++;
    }
}

static void on_location_changed(GClueSimple *simple, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    send_location(user_data, gclue_simple_get_location(simple));
}

static void on_simple_ready(GObject *source, GAsyncResult *res, gpointer user_data) {
    (void)source;
    WorkoutGps *g = user_data;
    GError *err = NULL;
    GClueSimple *simple = gclue_simple_new_with_thresholds_finish(res, &err);
    if (simple == NULL) {
        const gboolean cancelled = g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED);
        if (!cancelled) {
            reply_unavailable(g->state, err != NULL ? err->message : "GeoClue error");
            miwear_workout_gps_stop(g->state, NULL);
        }
        g_clear_error(&err);
        return; /* when cancelled, @g was already freed by stop() */
    }
    g->simple = simple;
    GClueLocation *first = gclue_simple_get_location(simple);
    if (!location_usable(first)) {
        gchar *why = first != NULL ? g_strdup_printf("best fix ±%.0f m", gclue_location_get_accuracy(first))
                                   : g_strdup("no fix");
        reply_unavailable(g->state, why);
        g_free(why);
        miwear_workout_gps_stop(g->state, NULL);
        return;
    }
    send_open_reply(g->state, TRUE);
    append_log(g->state, "Workout GPS: sharing this computer's location with the band.");
    g->location_handler = g_signal_connect(simple, "notify::location", G_CALLBACK(on_location_changed), g);
    send_location(g, gclue_simple_get_location(simple));
}

static void start(AppState *state, uint32_t sport) {
    if (s_gps != NULL) {
        /* Already sharing (the band asked again): confirm. Still starting: the answer follows. */
        if (s_gps->simple != NULL) {
            send_open_reply(state, TRUE);
        }
        return;
    }
    gchar *line = g_strdup_printf("Workout GPS: band started an outdoor workout (sport %u) and asked for location.", sport);
    append_log(state, line);
    g_free(line);
    if (!state->share_location_with_band) {
        reply_unavailable(state, "sharing is off on the Device page");
        return;
    }
    WorkoutGps *g = g_new0(WorkoutGps, 1);
    g->state = state;
    g->cancel = g_cancellable_new();
    s_gps = g;
    /* Updates at most once a second, from any distance moved. */
    gclue_simple_new_with_thresholds(GEOCLUE_DESKTOP_ID, GCLUE_ACCURACY_LEVEL_EXACT, 1, 0, g->cancel,
                                     on_simple_ready, g);
}

void miwear_workout_gps_stop(AppState *state, const char *reason) {
    WorkoutGps *g = s_gps;
    if (g == NULL) {
        return;
    }
    s_gps = NULL;
    g_cancellable_cancel(g->cancel);
    g_object_unref(g->cancel);
    if (g->simple != NULL) {
        if (g->location_handler != 0) {
            g_signal_handler_disconnect(g->simple, g->location_handler);
        }
        g_object_unref(g->simple); /* stops the GeoClue client */
    }
    if (reason != NULL && state != NULL) {
        gchar *line = g_strdup_printf("Workout GPS: stopped (%s) after %u location(s).", reason, g->sent);
        append_log(state, line);
        g_free(line);
    }
    g_free(g);
}

void miwear_workout_gps_handle_command(AppState *state, const struct Xiaomi__Command *cmd) {
    if (state == NULL || cmd == NULL || cmd->type != HEALTH_CMD_TYPE || !cmd->has_subtype || cmd->health == NULL) {
        return;
    }
    if (cmd->subtype == CMD_WORKOUT_WATCH_OPEN && cmd->health->workoutopenwatch != NULL) {
        const Xiaomi__WorkoutOpenWatch *o = cmd->health->workoutopenwatch;
        start(state, o->has_sport ? o->sport : 0);
    } else if (cmd->subtype == CMD_WORKOUT_WATCH_STATUS && cmd->health->workoutstatuswatch != NULL) {
        const Xiaomi__WorkoutStatusWatch *w = cmd->health->workoutstatuswatch;
        static const char *names[] = {"started", "resumed", "paused", "finished"};
        gchar *line = g_strdup_printf("Workout status: sport %u %s", w->has_sport ? w->sport : 0,
                                      w->has_status && w->status <= 3 ? names[w->status] : "?");
        append_log(state, line);
        g_free(line);
        if (w->has_status && w->status == WORKOUT_STATUS_FINISHED) {
            miwear_workout_gps_stop(state, "workout finished");
        }
    }
}
