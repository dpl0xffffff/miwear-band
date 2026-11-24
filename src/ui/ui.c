#include "salus/app.h"
#include "salus/config.h"
#include "salus/ui/ui.h"
#include "salus/store/workout.h"
#include "salus/services/notifications.h"
#include "salus/services/find_phone.h"
#include "salus/ui/dashboard.h"
#include "ui_private.h"
#include "salus/store/health_data.h"
#include "salus/store/sleep.h"
#include "salus/store/db.h"
#include "salus/local_time.h"
#include "salus/services/weather.h"

#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <cairo.h>
#include <gio/gio.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <adwaita.h>


typedef struct {
    SalusApp *state;
    gchar *status;
} UiStatusLine;

typedef struct {
    SalusApp *state;
    gchar *charging_text;
    gchar *wearing_text;
    gchar *sleep_text;
} UiStateUpdate;

typedef struct {
    SalusApp *state;
    gboolean busy;
} UiBusyState;

gboolean salus_ui_has_saved_device(SalusApp *state) {
    return state != NULL && state->saved_devices != NULL && state->saved_devices->len > 0;
}

void salus_ui_show_setup(SalusApp *state) {
    salus_connect_show(state);
}

#define APP_LOG_MAX_CHARS 300000

static gboolean ui_flush_log_queue_idle(gpointer user_data) {
    SalusApp *state = user_data;
    g_mutex_lock(&state->log_lock);
    GQueue *lines = state->log_queue;
    state->log_queue = NULL;
    state->log_flush_id = 0;
    g_mutex_unlock(&state->log_lock);

    GString *chunk = g_string_new(NULL);
    for (GList *l = lines != NULL ? lines->head : NULL; l != NULL; l = l->next) {
        g_string_append(chunk, l->data);
        g_string_append_c(chunk, '\n');
    }
    if (lines != NULL) {
        g_queue_free_full(lines, g_free);
    }

    if (chunk->len > 0 && state->log_buffer != NULL) {
        GtkTextIter end;
        gtk_text_buffer_begin_user_action(state->log_buffer);
        gtk_text_buffer_get_end_iter(state->log_buffer, &end);
        gtk_text_buffer_insert(state->log_buffer, &end, chunk->str, (gint)chunk->len);
        gint nchars = gtk_text_buffer_get_char_count(state->log_buffer);
        if (nchars > APP_LOG_MAX_CHARS) {
            GtkTextIter start, cut;
            gtk_text_buffer_get_start_iter(state->log_buffer, &start);
            gtk_text_buffer_get_iter_at_offset(state->log_buffer, &cut, nchars - APP_LOG_MAX_CHARS);
            gtk_text_buffer_delete(state->log_buffer, &start, &cut);
        }
        gtk_text_buffer_end_user_action(state->log_buffer);
    }
    g_string_free(chunk, TRUE);
    return G_SOURCE_REMOVE;
}

void append_log(SalusApp *state, const gchar *line) {
    if (line == NULL) return;
    g_mutex_lock(&state->log_lock);
    if (state->log_queue == NULL) {
        state->log_queue = g_queue_new();
    }
    g_queue_push_tail(state->log_queue, g_strdup(line));
    if (state->log_flush_id == 0) {
        state->log_flush_id = g_idle_add(ui_flush_log_queue_idle, state);
    }
    g_mutex_unlock(&state->log_lock);
}

static gboolean ui_set_status_cb(gpointer user_data) {
    UiStatusLine *ctx = user_data;
    salus_connect_set_status(ctx->state, ctx->status);
    salus_dashboard_set_status(ctx->state, ctx->status);
    g_free(ctx->status);
    g_free(ctx);
    return G_SOURCE_REMOVE;
}

static gboolean ui_set_busy_cb(gpointer user_data) {
    UiBusyState *ctx = user_data;
    salus_connect_set_busy(ctx->state, ctx->busy);
    salus_dashboard_set_busy(ctx->state, ctx->busy);
    g_free(ctx);
    return G_SOURCE_REMOVE;
}

void set_connect_busy(SalusApp *state, gboolean busy) {
    UiBusyState *ctx = g_new0(UiBusyState, 1);
    ctx->state = state;
    ctx->busy = busy;
    g_idle_add(ui_set_busy_cb, ctx);
}

void set_status(SalusApp *state, const gchar *status) {
    UiStatusLine *ctx = g_new0(UiStatusLine, 1);
    ctx->state = state;
    ctx->status = g_strdup(status);
    g_idle_add(ui_set_status_cb, ctx);
}

void salus_live_metrics_update(SalusApp *state, const SalusLiveMetrics *m) {
    if (m->has_hr) {
        state->live_hr = m->hr;
        state->has_live_hr = TRUE;
    }
    if (m->has_steps) {
        state->live_steps = m->steps;
        state->has_live_steps = TRUE;
    }
    if (m->has_battery && state->dash_ui != NULL) {
        state->dash_ui->battery = (int)m->battery;
    }
    salus_dashboard_schedule_refresh(state);
}

static gboolean ui_update_state_cb(gpointer user_data) {
    UiStateUpdate *u = user_data;
    salus_dashboard_update_device_state(u->state, u->charging_text, u->wearing_text, u->sleep_text);
    g_free(u->charging_text);
    g_free(u->wearing_text);
    g_free(u->sleep_text);
    g_free(u);
    return G_SOURCE_REMOVE;
}

void schedule_state_update(SalusApp *state, const gchar *charging, const gchar *wearing, const gchar *sleep) {
    UiStateUpdate *u = g_new0(UiStateUpdate, 1);
    u->state = state;
    u->charging_text = charging ? g_strdup(charging) : NULL;
    u->wearing_text = wearing ? g_strdup(wearing) : NULL;
    u->sleep_text = sleep ? g_strdup(sleep) : NULL;
    g_idle_add(ui_update_state_cb, u);
}


void salus_ui_reload_data(SalusApp *state) {
    recorded_rebuild_from_db(state);
    salus_sport_load_from_db(state);
    salus_dashboard_schedule_refresh(state);
}

static void show_main(SalusApp *state) {
    salus_ui_reload_data(state);
    if (state->stack != NULL) {
        gtk_stack_set_visible_child_name(GTK_STACK(state->stack), "main");
    }
    salus_dashboard_update_device_state(state, NULL, NULL, NULL);
    salus_dashboard_schedule_refresh(state);
}

/* The device connected. */
void switch_to_dashboard(SalusApp *state) {
    show_main(state);
    gchar *msg = g_strdup_printf("Connected to %s",
                                 state->device_display_name != NULL ? state->device_display_name : "the band");
    salus_dashboard_toast(state, msg);
    g_free(msg);
}

/* Connection lost: saved data stays browsable; only fall back to setup without a saved band. */
void switch_to_pairing(SalusApp *state) {
    if (salus_ui_has_saved_device(state)) {
        salus_dashboard_toast(state, "Connection to the band was lost");
        salus_dashboard_schedule_refresh(state);
    } else {
        salus_ui_show_setup(state);
    }
}

void salus_app_shutdown(GApplication *app, gpointer user_data) {
    (void)app;
    SalusApp *state = user_data;
    salus_find_phone_shutdown(state);
    salus_weather_shutdown(state);
    salus_app_set_device(state, NULL); /* disconnects and frees the driver session */
    g_clear_pointer(&state->saved_devices, g_ptr_array_unref);
    if (state->log_flush_id != 0) {
        g_source_remove(state->log_flush_id);
        state->log_flush_id = 0;
    }
    g_mutex_lock(&state->log_lock);
    if (state->log_queue != NULL) {
        while (g_queue_get_length(state->log_queue) > 0) {
            g_free(g_queue_pop_head(state->log_queue));
        }
        g_queue_free(state->log_queue);
        state->log_queue = NULL;
    }
    g_mutex_unlock(&state->log_lock);
    if (state->recorded_rebuild_source_id != 0) {
        g_source_remove(state->recorded_rebuild_source_id);
        state->recorded_rebuild_source_id = 0;
    }
    g_mutex_lock(&state->recorded_lock);
    if (state->recorded_days != NULL) {
        g_hash_table_unref(state->recorded_days);
        state->recorded_days = NULL;
    }
    if (state->daily_summaries != NULL) {
        g_hash_table_unref(state->daily_summaries);
        state->daily_summaries = NULL;
    }
    if (state->sleep_duration_by_day != NULL) {
        g_hash_table_unref(state->sleep_duration_by_day);
        state->sleep_duration_by_day = NULL;
    }
    g_mutex_unlock(&state->recorded_lock);
    g_free(state->weather_location_name);
    g_free(state->weather_last_status);
    g_free(state->notification_status);
    salus_notifications_shutdown(state);
    state->weather_location_name = NULL;
    state->weather_last_status = NULL;
    if (state->health_view_date != NULL) {
        g_date_free(state->health_view_date);
        state->health_view_date = NULL;
    }
    salus_sport_entries_clear(state);
    if (state->dash_ui != NULL) {
        salus_dashboard_ui_free(state->dash_ui);
        state->dash_ui = NULL;
    }
    salus_connect_free(state);
    salus_db_close();
}

void salus_activate(GApplication *application, gpointer user_data) {
    SalusApp *state = user_data;
    if (state->window != NULL) {
        gtk_window_present(GTK_WINDOW(state->window));
        return;
    }
    if (!salus_db_open()) {
        g_warning("Gadgetbridge-compatible database could not be opened; samples will not be persisted.");
    }
    ui_style_install(gdk_display_get_default());
    state->log_buffer = gtk_text_buffer_new(NULL);

    state->window = adw_application_window_new(GTK_APPLICATION(application));
    gtk_window_set_title(GTK_WINDOW(state->window), "Salus");
    gtk_window_set_default_size(GTK_WINDOW(state->window), 980, 820);
    gtk_widget_set_size_request(state->window, 360, 480);


    state->dash_ui = salus_dashboard_ui_new();
    state->page_dashboard = salus_dashboard_build(state, state->dash_ui);
    state->page_connect = salus_connect_build(state);

    state->stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(state->stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_add_named(GTK_STACK(state->stack), state->page_dashboard, "main");
    gtk_stack_add_named(GTK_STACK(state->stack), state->page_connect, "setup");

    state->toast_overlay = adw_toast_overlay_new();
    adw_toast_overlay_set_child(ADW_TOAST_OVERLAY(state->toast_overlay), state->stack);
    salus_dashboard_set_toast_overlay(state->dash_ui, state->toast_overlay);
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(state->window), state->toast_overlay);
    salus_dashboard_add_breakpoints(state->window, state->dash_ui);

    if (salus_ui_has_saved_device(state)) {
        show_main(state);
    } else {
        salus_ui_show_setup(state);
    }
    gtk_window_present(GTK_WINDOW(state->window));

    if (state->notifications_enabled) {
        salus_notifications_start(state);
    }
    salus_dashboard_schedule_refresh(state);
}
