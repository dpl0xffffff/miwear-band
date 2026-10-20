#include "miwear/app.h"
#include "miwear/config.h"
#include "miwear/profile.h"
#include "miwear/weight_log.h"
#include "miwear/notifications/notifications.h"
#include "miwear/ui/ui.h"

#include <stdlib.h>

#include <glib.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <adwaita.h>

int main(int argc, char **argv) {
    AppState state = {0};
    state.sock_fd = -1;
    state.goal_moving = 30;
    state.selected_recorded_stat = REC_STAT_HEART_RATE;
    state.auth_key_hex[0] = '\0';
    state.verbose_wire_log = (g_getenv("MIWEAR_VERBOSE_WIRE_LOG") != NULL);
    state.verbose_fetch_log = (g_getenv("MIWEAR_FETCH_DEBUG") != NULL);
    g_mutex_init(&state.recorded_lock);
    g_mutex_init(&state.log_lock);

    if (!miwear_config_load(&state, NULL)) {
        g_strlcpy(state.auth_key_hex, DEFAULT_AUTH_KEY_HEX, sizeof(state.auth_key_hex));
        state.notifications_enabled = TRUE;
        state.notifications_min_interval_ms = 3000;
        state.notification_next_id = 1;
    }
    miwear_profile_apply_defaults(&state);
    miwear_weight_log_load(&state);

    state.app = adw_application_new("com.miwear.band", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(state.app, "activate", G_CALLBACK(miwear_activate), &state);
    g_signal_connect(state.app, "shutdown", G_CALLBACK(miwear_app_shutdown), &state);

    const int status = g_application_run(G_APPLICATION(state.app), argc, argv);
    g_object_unref(state.app);
    g_mutex_clear(&state.recorded_lock);
    g_mutex_clear(&state.log_lock);
    g_free(state.selected_address);
    g_free(state.device_display_name);
    g_free(state.device_status_wearing);
    g_free(state.device_status_charging);
    g_free(state.device_status_sleep);
    return status;
}
