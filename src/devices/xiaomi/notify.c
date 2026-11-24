#include "salus/devices/xiaomi/notify.h"

#include "salus/devices/xiaomi/wire_internal.h"

#include <string.h>

#include "xiaomi.pb-c.h"

/* Desktop notifications → band (Notification3, as Gadgetbridge sends them). */

#define NOTIF_CMD_TYPE 7u
#define NOTIF_SUBTYPE_SEND 0u
#define NOTIF_SUBTYPE_DISMISS 1u
#define NOTIF_SUBTYPE_SCREEN_ON_GET 6u

#define TITLE_MAX 120
#define BODY_MAX 240

/* Gadgetbridge: yyyyMMdd'T'HHmmss (local). Other shapes can crash Band 9/10 firmware. */
static gchar *format_band_timestamp_from_unix(gint64 unix_sec) {
    GDateTime *dt = unix_sec > 0
                        ? g_date_time_new_from_unix_local(unix_sec)
                        : g_date_time_new_now_local();
    const gint y = g_date_time_get_year(dt);
    const gint mo = g_date_time_get_month(dt);
    const gint d = g_date_time_get_day_of_month(dt);
    const gint h = g_date_time_get_hour(dt);
    const gint mi = g_date_time_get_minute(dt);
    const gint s = g_date_time_get_second(dt);
    g_date_time_unref(dt);
    return g_strdup_printf("%04d%02d%02dT%02d%02d%02d", y, mo, d, h, mi, s);
}

static gchar *truncate_utf8_copy(const char *s, gsize max_chars) {
    if (s == NULL) {
        return g_strdup("");
    }
    glong len = g_utf8_strlen(s, -1);
    if (len <= (glong)max_chars) {
        return g_strdup(s);
    }
    return g_utf8_substring(s, 0, (glong)max_chars);
}

gboolean xiaomi_send_notification(SalusDevice *dev, const SalusNotification *n) {
    SalusApp *state = dev->app;
    const guint32 band_id = n->id;
    const char *title = n->title, *body = n->body;

    gchar *title_buf = truncate_utf8_copy(title, TITLE_MAX);
    gchar *body_buf = truncate_utf8_copy(body, BODY_MAX);
    if (body_buf[0] == '\0' && title_buf[0] != '\0') {
        g_free(body_buf);
        body_buf = g_strdup(title_buf);
    }
    if (body_buf[0] == '\0') {
        g_free(body_buf);
        body_buf = g_strdup(" ");
    }

    gchar *ts = format_band_timestamp_from_unix(n->when_unix_sec);
    gchar *pkg = g_strdup(n->app_id != NULL ? n->app_id : "io.salus.unknown");
    gchar *app_disp = truncate_utf8_copy(n->app_name, 64);
    if (app_disp[0] == '\0') {
        g_free(app_disp);
        app_disp = g_strdup("UNKNOWN");
    }

    Xiaomi__Notification3 n3 = XIAOMI__NOTIFICATION3__INIT;
    n3.package = pkg;
    n3.appname = app_disp;
    n3.title = title_buf;
    n3.unknown4 = (gchar *)"";
    n3.body = body_buf;
    n3.timestamp = ts;
    n3.has_id = TRUE;
    n3.id = band_id;

    Xiaomi__Notification2 n2 = XIAOMI__NOTIFICATION2__INIT;
    n2.notification3 = &n3;

    Xiaomi__Notification notif = XIAOMI__NOTIFICATION__INIT;
    notif.notification2 = &n2;

    Xiaomi__Command cmd = XIAOMI__COMMAND__INIT;
    cmd.type = NOTIF_CMD_TYPE;
    cmd.has_subtype = TRUE;
    cmd.subtype = NOTIF_SUBTYPE_SEND;
    cmd.notification = &notif;

    gchar *label = g_strdup_printf("Notification → band (#%u, ts=%s)", band_id, ts);
    const gboolean ok = salus_wire_send_xiaomi_command(state, &cmd, label);
    g_free(label);

    g_free(ts);
    g_free(pkg);
    g_free(app_disp);
    g_free(title_buf);
    g_free(body_buf);
    return ok;
}

void xiaomi_request_notification_prefs(SalusApp *state) {
    if (state == NULL || !salus_device_connected(state)) {
        return;
    }
    Xiaomi__Notification n = XIAOMI__NOTIFICATION__INIT;
    Xiaomi__Command cmd = XIAOMI__COMMAND__INIT;
    cmd.type = NOTIF_CMD_TYPE;
    cmd.has_subtype = TRUE;
    cmd.subtype = NOTIF_SUBTYPE_SCREEN_ON_GET;
    cmd.notification = &n;
    (void)salus_wire_send_xiaomi_command(state, &cmd, "Notification prefs (screen on)");
}

void xiaomi_notifications_handle_command(SalusApp *state, const Xiaomi__Command *cmd) {
    if (state == NULL || cmd == NULL || cmd->type != NOTIF_CMD_TYPE) {
        return;
    }
    if (cmd->has_subtype && cmd->subtype == NOTIF_SUBTYPE_DISMISS && cmd->notification != NULL &&
        cmd->notification->notificationdismiss != NULL) {
        append_log(state, "Band dismissed notification(s) on phone side (not acted on desktop yet).");
    }
}

