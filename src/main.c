#include "salus/app.h"
#include "salus/config.h"
#include "salus/migrate.h"
#include "salus/profile.h"
#include "salus/weight_log.h"
#include "salus/notifications/notifications.h"
#include "salus/ui/ui.h"

#include <stdlib.h>

#include <glib.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <adwaita.h>

int main(int argc, char **argv) {
    SalusApp state = {0};
    state.goal_moving = 30;
    g_mutex_init(&state.recorded_lock);
    g_mutex_init(&state.log_lock);

    salus_migrate_legacy_files();
    if (!salus_config_load(&state, NULL)) {
        state.notifications_enabled = TRUE;
        state.notifications_min_interval_ms = 3000;
        state.notification_next_id = 1;
    }
    salus_profile_apply_defaults(&state);
    salus_weight_log_load(&state);

    state.app = adw_application_new("io.salus.Salus", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(state.app, "activate", G_CALLBACK(salus_activate), &state);
    g_signal_connect(state.app, "shutdown", G_CALLBACK(salus_app_shutdown), &state);

    const int status = g_application_run(G_APPLICATION(state.app), argc, argv);
    g_object_unref(state.app);
    g_mutex_clear(&state.recorded_lock);
    g_mutex_clear(&state.log_lock);
    g_free(state.selected_address);
    g_free(state.device_display_name);
    g_free(state.device_status_wearing);
    g_free(state.device_status_charging);
    g_free(state.device_status_sleep);
    if (state.weight_log != NULL) {
        g_array_unref(state.weight_log);
    }
    return status;
}
