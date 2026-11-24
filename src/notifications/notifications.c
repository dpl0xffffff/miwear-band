#include "salus/notifications/notifications.h"
#include "salus/config.h"
#include "salus/devices/xiaomi/wire_internal.h"
#include "salus/ui/dashboard.h"

#include <string.h>

#include <gio/gio.h>

#include "xiaomi.pb-c.h"

#define NOTIF_CMD_TYPE 7u
#define NOTIF_SUBTYPE_SEND 0u
#define NOTIF_SUBTYPE_DISMISS 1u
#define NOTIF_SUBTYPE_SCREEN_ON_GET 6u

#define TITLE_MAX 120
#define BODY_MAX 240
#define PACKAGE_MAX 64

static void notif_status_update(SalusApp *state, const char *msg) {
    if (state == NULL || msg == NULL) {
        return;
    }
    g_free(state->notification_status);
    state->notification_status = g_strdup(msg);
    salus_dashboard_set_notification_status(state, msg);
}

static void log_desktop_capture(SalusApp *state, const char *via, const char *app, const char *title) {
    gchar *line =
        g_strdup_printf("Desktop notify (%s): %s — %s", via, app != NULL ? app : "?", title != NULL ? title : "");
    append_log(state, line);
    g_free(line);
}

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

static gint64 unix_sec_from_notify_hints(GVariant *hints) {
    if (hints == NULL) {
        return 0;
    }
    GVariant *v = g_variant_lookup_value(hints, "timestamp", G_VARIANT_TYPE_INT64);
    if (v == NULL) {
        v = g_variant_lookup_value(hints, "timestamp", G_VARIANT_TYPE_UINT64);
    }
    if (v == NULL) {
        v = g_variant_lookup_value(hints, "timestamp", G_VARIANT_TYPE_INT32);
    }
    if (v == NULL) {
        v = g_variant_lookup_value(hints, "timestamp", G_VARIANT_TYPE_UINT32);
    }
    if (v == NULL) {
        v = g_variant_lookup_value(hints, "x-notify-received-time", G_VARIANT_TYPE_UINT64);
    }
    if (v == NULL) {
        return 0;
    }
    gint64 raw = 0;
    switch (g_variant_classify(v)) {
    case G_VARIANT_CLASS_INT64:
        raw = g_variant_get_int64(v);
        break;
    case G_VARIANT_CLASS_UINT64:
        raw = (gint64)g_variant_get_uint64(v);
        break;
    case G_VARIANT_CLASS_INT32:
        raw = g_variant_get_int32(v);
        break;
    case G_VARIANT_CLASS_UINT32:
        raw = (gint64)g_variant_get_uint32(v);
        break;
    default:
        break;
    }
    g_variant_unref(v);
    if (raw <= 0) {
        return 0;
    }
    if (raw > 100000000000LL) {
        raw = raw / 1000;
    }
    GDateTime *dt = g_date_time_new_from_unix_local(raw);
    const gint y = g_date_time_get_year(dt);
    g_date_time_unref(dt);
    if (y < 2020 || y > 2100) {
        return 0;
    }
    return raw;
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

static gchar *variant_dict_string_dup(GVariant *dict, const char *key) {
    if (dict == NULL || key == NULL) {
        return NULL;
    }
    GVariant *v = g_variant_lookup_value(dict, key, G_VARIANT_TYPE_STRING);
    if (v == NULL) {
        return NULL;
    }
    gchar *out = g_strdup(g_variant_get_string(v, NULL));
    g_variant_unref(v);
    return out;
}

static gchar *package_from_app_name(const char *app_name);

static gchar *package_for_notification(const char *app_name, GVariant *hints) {
    gchar *entry = variant_dict_string_dup(hints, "desktop-entry");
    const char *src = (entry != NULL && entry[0] != '\0') ? entry : app_name;
    if (src != NULL && strchr(src, '.') != NULL &&
        (g_str_has_prefix(src, "com.") || g_str_has_prefix(src, "org.") || g_str_has_prefix(src, "io.") ||
         g_str_has_prefix(src, "net."))) {
        gchar *pkg = g_strdup(src);
        g_free(entry);
        return pkg;
    }
    g_free(entry);
    return package_from_app_name(app_name);
}

static gchar *display_name_for_notification(const char *app_name, GVariant *hints) {
    gchar *name = variant_dict_string_dup(hints, "application-name");
    if (name != NULL && name[0] != '\0') {
        return name;
    }
    g_free(name);
    name = variant_dict_string_dup(hints, "desktop-entry");
    if (name != NULL && name[0] != '\0') {
        return name;
    }
    g_free(name);
    if (app_name != NULL && app_name[0] != '\0' && app_name[0] != ':') {
        return g_strdup(app_name);
    }
    return g_strdup("Linux");
}

static gchar *package_from_app_name(const char *app_name) {
    GString *s = g_string_new("io.salus.");
    const char *src = (app_name != NULL && app_name[0] != '\0') ? app_name : "unknown";
    for (const char *p = src; *p && s->len < PACKAGE_MAX - 1; p++) {
        char c = *p;
        if (g_ascii_isalnum(c) || c == '.' || c == '_') {
            g_string_append_c(s, (gchar)g_ascii_tolower(c));
        } else if (c == ' ' || c == '-') {
            g_string_append_c(s, '.');
        }
    }
    while (s->len > 6 && s->str[s->len - 1] == '.') {
        g_string_truncate(s, s->len - 1);
    }
    return g_string_free(s, FALSE);
}

/* Band notification ids only need to be unique per session (the id → package map that used to be
 * kept here was never read and grew by one entry per forwarded notification). */
static guint32 notification_next_band_id(SalusApp *state) {
    return state->notification_next_id++;
}

static gboolean send_notification3(SalusApp *state,
                                   guint32 band_id,
                                   const char *package,
                                   const char *app_name,
                                   const char *title,
                                   const char *body,
                                   gint64 when_unix_sec,
                                   GVariant *notify_hints) {
    if (state == NULL || !salus_device_connected(state)) {
        return FALSE;
    }

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

    gchar *ts = format_band_timestamp_from_unix(when_unix_sec);
    gchar *pkg = package != NULL ? g_strdup(package) : package_for_notification(app_name, notify_hints);
    gchar *app_label = display_name_for_notification(app_name, notify_hints);
    gchar *app_disp = truncate_utf8_copy(app_label, 64);
    g_free(app_label);
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

    Xiaomi__Notification n = XIAOMI__NOTIFICATION__INIT;
    n.notification2 = &n2;

    Xiaomi__Command cmd = XIAOMI__COMMAND__INIT;
    cmd.type = NOTIF_CMD_TYPE;
    cmd.has_subtype = TRUE;
    cmd.subtype = NOTIF_SUBTYPE_SEND;
    cmd.notification = &n;

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

static gboolean salus_notifications_push_at(SalusApp *state,
                                             const char *app_name,
                                             const char *title,
                                             const char *body,
                                             gint64 when_unix_sec,
                                             GVariant *notify_hints) {
    if (state == NULL || !state->notifications_enabled) {
        return FALSE;
    }
    if (!salus_device_connected(state)) {
        append_log(state, "Desktop notification captured but not sent (connect band first).");
        return FALSE;
    }

    const gint64 now_ms = g_get_monotonic_time() / 1000;
    if (state->notification_last_sent_ms > 0 &&
        (now_ms - state->notification_last_sent_ms) < (gint64)state->notifications_min_interval_ms) {
        append_log(state, "Notification skipped (rate limit).");
        return FALSE;
    }

    gchar *pkg = package_for_notification(app_name, notify_hints);
    const guint32 band_id = notification_next_band_id(state);

    const gboolean ok =
        send_notification3(state, band_id, pkg, app_name, title, body, when_unix_sec, notify_hints);
    g_free(pkg);
    if (ok) {
        state->notification_last_sent_ms = now_ms;
        gchar *line = g_strdup_printf("Forwarded: %s — %s", app_name != NULL ? app_name : "?", title != NULL ? title : "");
        append_log(state, line);
        g_free(line);
    } else {
        append_log(state, "Desktop notification captured but not sent (connect band first).");
    }
    return ok;
}

typedef struct {
    SalusApp *state;
    gchar *app_name;
    gchar *title;
    gchar *body;
    gint64 when_unix_sec;
    GVariant *notify_hints;
} NotifPushIdle;

static gboolean notif_push_idle(gpointer user_data) {
    NotifPushIdle *ctx = user_data;
    (void)salus_notifications_push_at(ctx->state, ctx->app_name, ctx->title, ctx->body, ctx->when_unix_sec,
                                       ctx->notify_hints);
    g_free(ctx->app_name);
    g_free(ctx->title);
    g_free(ctx->body);
    if (ctx->notify_hints != NULL) {
        g_variant_unref(ctx->notify_hints);
    }
    g_free(ctx);
    return G_SOURCE_REMOVE;
}

static void schedule_push(SalusApp *state,
                          const char *app_name,
                          const char *title,
                          const char *body,
                          gint64 when_unix_sec,
                          GVariant *notify_hints) {
    if (state == NULL) {
        return;
    }
    if (app_name != NULL && g_str_has_prefix(app_name, "salus")) {
        return;
    }
    NotifPushIdle *ctx = g_new0(NotifPushIdle, 1);
    ctx->state = state;
    ctx->app_name = g_strdup(app_name != NULL ? app_name : "Linux");
    ctx->title = g_strdup(title != NULL ? title : "");
    ctx->body = g_strdup(body != NULL ? body : "");
    ctx->when_unix_sec = when_unix_sec;
    ctx->notify_hints = notify_hints != NULL ? g_variant_ref(notify_hints) : NULL;
    g_idle_add(notif_push_idle, ctx);
}

static void handle_freedesktop_notify(SalusApp *state, GDBusMessage *message) {
    GVariant *params = g_dbus_message_get_body(message);
    if (params == NULL || !g_variant_is_of_type(params, G_VARIANT_TYPE("(susssasa{sv}i)"))) {
        return;
    }

    const char *app_name = NULL;
    const char *summary = NULL;
    const char *body = NULL;
    GVariant *hints = NULL;
    g_variant_get(params, "(&su&s&s&sas@a{sv}i)", &app_name, NULL, NULL, &summary, &body, NULL, &hints, NULL);
    const gint64 when_sec = unix_sec_from_notify_hints(hints);

    if (app_name == NULL || app_name[0] == '\0') {
        app_name = g_dbus_message_get_sender(message);
    }

    log_desktop_capture(state, "dunst", app_name, summary);
    schedule_push(state, app_name, summary, body, when_sec, hints);
    if (hints != NULL) {
        g_variant_unref(hints);
    }
}

static void handle_portal_add_notification(SalusApp *state, GDBusMessage *message) {
    GVariant *params = g_dbus_message_get_body(message);
    if (params == NULL || !g_variant_is_of_type(params, G_VARIANT_TYPE("(sa{sv})"))) {
        return;
    }

    const char *id = NULL;
    GVariant *dict = NULL;
    g_variant_get(params, "(&s@a{sv})", &id, &dict);
    if (dict == NULL || !g_variant_is_of_type(dict, G_VARIANT_TYPE("a{sv}"))) {
        g_clear_pointer(&dict, g_variant_unref);
        return;
    }
    gchar *title = variant_dict_string_dup(dict, "title");
    gchar *body = variant_dict_string_dup(dict, "body");
    if (body == NULL) {
        body = variant_dict_string_dup(dict, "markup-body");
    }
    const char *sender = g_dbus_message_get_sender(message);
    const char *app = (id != NULL && id[0] != '\0') ? id : sender;

    log_desktop_capture(state, "portal", app, title);
    schedule_push(state, app, title, body, 0, NULL);
    g_free(title);
    g_free(body);
    if (dict != NULL) {
        g_variant_unref(dict);
    }
}

static GDBusMessage *dbus_filter_drop_outgoing(GDBusConnection *connection,
                                               GDBusMessage *message,
                                               gboolean incoming,
                                               gpointer user_data) {
    (void)connection;
    SalusApp *state = user_data;
    if (!incoming && state != NULL && state->notification_monitor_active) {
        return NULL;
    }
    return message;
}

static GDBusMessage *dbus_message_filter(GDBusConnection *connection,
                                         GDBusMessage *message,
                                         gboolean incoming,
                                         gpointer user_data) {
    (void)connection;
    SalusApp *state = user_data;
    if (!incoming || state == NULL || !state->notifications_enabled) {
        return message;
    }

    if (g_dbus_message_get_message_type(message) != G_DBUS_MESSAGE_TYPE_METHOD_CALL) {
        return message;
    }

    const char *iface = g_dbus_message_get_interface(message);
    const char *member = g_dbus_message_get_member(message);
    if (g_strcmp0(iface, "org.freedesktop.Notifications") == 0 && g_strcmp0(member, "Notify") == 0) {
        handle_freedesktop_notify(state, message);
    } else if (g_strcmp0(iface, "org.freedesktop.portal.Notification") == 0 &&
               g_strcmp0(member, "AddNotification") == 0) {
        handle_portal_add_notification(state, message);
    }

    return message;
}

static GDBusConnection *open_notification_monitor_bus(GError **error) {
    gchar *address = g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SESSION, NULL, error);
    if (address == NULL) {
        return NULL;
    }
    GDBusConnection *conn = g_dbus_connection_new_for_address_sync(
        address,
        G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
        NULL,
        NULL,
        error);
    g_free(address);
    return conn;
}

/* dbus-broker: flags must be 0; dunst needs path match; portal needs eavesdrop in the rule string. */
static gboolean become_notification_monitor(GDBusConnection *conn, GError **error) {
    static const gchar *const monitor_rules[] = {
        "type='method_call',path='/org/freedesktop/Notifications',"
        "interface='org.freedesktop.Notifications',member='Notify'",
        "eavesdrop=true,type='method_call',interface='org.freedesktop.portal.Notification',member='AddNotification'",
        NULL,
    };
    GVariant *rules_arr = g_variant_new_strv(monitor_rules, -1);
    GVariant *children[] = {rules_arr, g_variant_new_uint32(0)};
    GVariant *body = g_variant_new_tuple(children, G_N_ELEMENTS(children));
    GVariant *reply = g_dbus_connection_call_sync(conn,
                                                  "org.freedesktop.DBus",
                                                  "/org/freedesktop/DBus",
                                                  "org.freedesktop.DBus.Monitoring",
                                                  "BecomeMonitor",
                                                  body,
                                                  NULL,
                                                  G_DBUS_CALL_FLAGS_NONE,
                                                  5000,
                                                  NULL,
                                                  error);
    g_variant_unref(body);
    if (reply != NULL) {
        g_variant_unref(reply);
        return TRUE;
    }
    return FALSE;
}

static void request_screen_on_pref(SalusApp *state) {
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

static void ensure_dbus_listener(SalusApp *state) {
    if (state == NULL || !state->notifications_enabled || state->notification_bus != NULL) {
        return;
    }
    GError *err = NULL;
    state->notification_bus = open_notification_monitor_bus(&err);
    if (state->notification_bus == NULL) {
        notif_status_update(state, err != NULL ? err->message : "Session D-Bus unavailable");
        g_clear_error(&err);
        return;
    }

    state->notification_bus_filter_id =
        g_dbus_connection_add_filter(state->notification_bus, dbus_message_filter, state, NULL);

    if (!become_notification_monitor(state->notification_bus, &err)) {
        notif_status_update(state, err != NULL ? err->message : "BecomeMonitor failed");
        append_log(state, err != NULL && err->message != NULL
                                   ? err->message
                                   : "Notification monitor: BecomeMonitor failed (desktop forward disabled).");
        g_clear_error(&err);
        g_dbus_connection_remove_filter(state->notification_bus, state->notification_bus_filter_id);
        state->notification_bus_filter_id = 0;
        g_object_unref(state->notification_bus);
        state->notification_bus = NULL;
        return;
    }

    state->notification_monitor_active = TRUE;
    state->notification_bus_outgoing_filter_id =
        g_dbus_connection_add_filter(state->notification_bus, dbus_filter_drop_outgoing, state, NULL);

    notif_status_update(state, "Monitoring dunst + portal notifications (Hyprland/Linux)");
    append_log(state, "Notification monitor active — dunst Notify + portal AddNotification.");
}

static void stop_dbus_listener(SalusApp *state) {
    if (state == NULL || state->notification_bus == NULL) {
        return;
    }
    if (state->notification_bus_filter_id != 0) {
        g_dbus_connection_remove_filter(state->notification_bus, state->notification_bus_filter_id);
        state->notification_bus_filter_id = 0;
    }
    if (state->notification_bus_outgoing_filter_id != 0) {
        g_dbus_connection_remove_filter(state->notification_bus, state->notification_bus_outgoing_filter_id);
        state->notification_bus_outgoing_filter_id = 0;
    }
    state->notification_monitor_active = FALSE;
    g_object_unref(state->notification_bus);
    state->notification_bus = NULL;
}

void salus_notifications_on_connect(SalusApp *state) {
    if (state == NULL || !state->notifications_enabled) {
        return;
    }
    ensure_dbus_listener(state);
    request_screen_on_pref(state);
}

void salus_notifications_on_disconnect(SalusApp *state) {
    if (state == NULL) {
        return;
    }
    state->notification_last_sent_ms = 0;
}

void salus_notifications_shutdown(SalusApp *state) {
    if (state == NULL) {
        return;
    }
    stop_dbus_listener(state);
}

void salus_notifications_handle_command(SalusApp *state, const Xiaomi__Command *cmd) {
    if (state == NULL || cmd == NULL || cmd->type != NOTIF_CMD_TYPE) {
        return;
    }
    if (cmd->has_subtype && cmd->subtype == NOTIF_SUBTYPE_DISMISS && cmd->notification != NULL &&
        cmd->notification->notificationdismiss != NULL) {
        append_log(state, "Band dismissed notification(s) on phone side (not acted on desktop yet).");
    }
}

void salus_notifications_set_enabled(SalusApp *state, gboolean enabled) {
    if (state == NULL) {
        return;
    }
    state->notifications_enabled = enabled;
    if (enabled) {
        ensure_dbus_listener(state);
        if (salus_device_connected(state)) {
            request_screen_on_pref(state);
        }
    } else {
        stop_dbus_listener(state);
        notif_status_update(state, "Desktop notification sync disabled");
    }
    (void)salus_config_save(state, NULL);
}

void salus_notifications_send_test(SalusApp *state) {
    if (state == NULL) {
        return;
    }
    static const char test_pkg[] = "io.salus.Salus";
    const guint32 band_id = notification_next_band_id(state);
    const gboolean ok = send_notification3(state,
                                           band_id,
                                           test_pkg,
                                           "Salus",
                                           "Test notification",
                                           "Linux desktop to band sync",
                                           0,
                                           NULL);
    if (ok) {
        state->notification_last_sent_ms = g_get_monotonic_time() / 1000;
    }
    notif_status_update(state,
                        ok ? "Test notification sent to band"
                           : "Could not send (connect band or wait for rate limit)");
}
