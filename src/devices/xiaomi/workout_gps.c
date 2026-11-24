#include "salus/devices/xiaomi/workout_gps.h"

#include "salus/devices/xiaomi/wire_internal.h"
#include "salus/services/location.h"

#include <math.h>

#include "xiaomi.pb-c.h"

/* Connected GPS, Xiaomi side: answers the band's "workout open" and streams the location
 * service's fixes as WorkoutLocation messages. */

#define HEALTH_CMD_TYPE 8u
#define CMD_WORKOUT_WATCH_STATUS 26u
#define CMD_WORKOUT_WATCH_OPEN 30u
#define CMD_WORKOUT_LOCATION 48u

#define WORKOUT_STATUS_FINISHED 3u

static guint s_sent; /* fixes sent in the current workout */

static void send_open_reply(SalusApp *state, gboolean have_location) {
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
    (void)salus_wire_send_xiaomi_command(state, &cmd,
                                          have_location ? "Workout GPS: replied with computer location available"
                                                        : "Workout GPS: replied without computer location");
}

/* No usable location. By default the app stays silent, as before connected GPS existed: the
 * "no permission" reply makes the band show "grant location permission on your phone". */
static void reply_unavailable(SalusApp *state, const char *why) {
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

static void send_location(SalusApp *state, const SalusLocationFix *fix) {
    Xiaomi__WorkoutLocation wl = XIAOMI__WORKOUT_LOCATION__INIT;
    wl.has_unknown1 = TRUE;
    wl.unknown1 = 10; /* Gadgetbridge: "number of satellites", always 10 */
    wl.has_timestamp = TRUE;
    wl.timestamp = (uint32_t)fix->timestamp;
    wl.has_latitude = wl.has_longitude = TRUE;
    wl.latitude = fix->latitude;
    wl.longitude = fix->longitude;
    if (!isnan(fix->altitude)) {
        wl.has_altitude = TRUE;
        wl.altitude = fix->altitude;
    }
    if (fix->speed >= 0) {
        wl.has_speed = TRUE;
        wl.speed = (float)fix->speed;
    }
    if (fix->heading >= 0) {
        wl.has_bearing = TRUE;
        wl.bearing = (float)fix->heading;
    }
    wl.has_horizontalaccuracy = TRUE;
    wl.horizontalaccuracy = (float)fix->accuracy;

    Xiaomi__Health health = XIAOMI__HEALTH__INIT;
    health.workoutlocation = &wl;
    Xiaomi__Command cmd = XIAOMI__COMMAND__INIT;
    cmd.type = HEALTH_CMD_TYPE;
    cmd.has_subtype = TRUE;
    cmd.subtype = CMD_WORKOUT_LOCATION;
    cmd.health = &health;
    const gboolean ok = salus_wire_send_xiaomi_command(state, &cmd, NULL);
    /* Log the first fix and then every 30th, so a long walk doesn't flood the log. */
    if (!ok || s_sent == 0 || s_sent % 30 == 0) {
        gchar *line = g_strdup_printf("Workout GPS: %s location #%u (%.5f, %.5f ±%.0f m)",
                                      ok ? "sent" : "failed to send", s_sent + 1, wl.latitude, wl.longitude,
                                      fix->accuracy);
        append_log(state, line);
        g_free(line);
    }
    if (ok) {
        s_sent++;
    }
}

static void on_location_ready(SalusApp *state, gboolean available, const char *why, gpointer user_data) {
    (void)user_data;
    if (available) {
        send_open_reply(state, TRUE);
    } else {
        reply_unavailable(state, why);
    }
}

static void on_location_fix(SalusApp *state, const SalusLocationFix *fix, gpointer user_data) {
    (void)user_data;
    if (salus_device_connected(state)) {
        send_location(state, fix);
    }
}

static void start(SalusApp *state, uint32_t sport) {
    if (salus_location_started()) {
        /* Already sharing (the band asked again): confirm. Still starting: the answer follows. */
        if (salus_location_active()) {
            send_open_reply(state, TRUE);
        }
        return;
    }
    gchar *line = g_strdup_printf("Workout GPS: band started an outdoor workout (sport %u) and asked for location.", sport);
    append_log(state, line);
    g_free(line);
    s_sent = 0;
    static const SalusLocationCallbacks callbacks = {.ready = on_location_ready, .fix = on_location_fix};
    salus_location_start(state, &callbacks, NULL);
}

void salus_workout_gps_handle_command(SalusApp *state, const struct Xiaomi__Command *cmd) {
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
            salus_location_stop(state, "workout finished");
        }
    }
}
