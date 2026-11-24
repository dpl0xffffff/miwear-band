#include "ui_private.h"

#include "miwear/config.h"
#include "miwear/find_phone.h"
#include "miwear/notifications/notifications.h"
#include "miwear/spp/fitness_sync.h"
#include "miwear/spp/weather.h"
#include "miwear/spp/workout_gps.h"
#include "miwear/spp/wire.h"
#include "miwear/spp/wire_internal.h"

#include <string.h>

static void on_connect_clicked(GtkButton *b, gpointer user_data) {
    (void)b;
    AppState *state = user_data;
    if (mi_is_connected(state)) {
        disconnect_socket(state);
        miwear_dashboard_set_status(state, "Disconnected");
        mi_toast(state, "Disconnected from the band");
        miwear_dashboard_refresh(state);
    } else {
        mi_connect_saved(state);
    }
}

static void on_sync_clicked(GtkButton *b, gpointer user_data) {
    (void)b;
    AppState *state = user_data;
    miwear_fitness_sync_resync(state);
    mi_toast(state, "Syncing health data…");
}

static void on_find_clicked(GtkButton *b, gpointer user_data) {
    (void)b;
    AppState *state = user_data;
    mi_toast(state, miwear_wire_find_band(state, TRUE) ? "The band is vibrating (about 45 s)"
                                                       : "Couldn't reach the band");
}

static void on_weather_clicked(GtkButton *b, gpointer user_data) {
    (void)b;
    miwear_weather_push_cached(user_data);
    mi_toast(user_data, "Sending weather to the band…");
}

static void on_find_phone_stop(GtkButton *b, gpointer user_data) {
    (void)b;
    miwear_find_phone_dismiss(user_data);
}

static void on_notif_toggled(GObject *obj, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    AppState *state = user_data;
    const gboolean on = adw_switch_row_get_active(ADW_SWITCH_ROW(obj));
    if (on != state->notifications_enabled) {
        miwear_notifications_set_enabled(state, on);
    }
}

static void on_share_location_toggled(GObject *obj, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    AppState *state = user_data;
    const gboolean on = adw_switch_row_get_active(ADW_SWITCH_ROW(obj));
    if (on == state->share_location_with_band) {
        return;
    }
    state->share_location_with_band = on;
    (void)miwear_config_save(state, NULL);
    if (!on) {
        miwear_workout_gps_stop(state, "location sharing turned off");
    }
}

static void on_notif_test(GtkButton *b, gpointer user_data) {
    (void)b;
    miwear_notifications_send_test(user_data);
}

static void on_change_device(AdwActionRow *row, gpointer user_data) {
    (void)row;
    miwear_ui_show_setup(user_data);
}

static void on_show_log(AdwActionRow *row, gpointer user_data) {
    (void)row;
    mi_show_log_dialog(user_data);
}

static GtkWidget *row_button(GtkWidget *row, const char *label, GCallback cb, gpointer data) {
    GtkWidget *b = gtk_button_new_with_label(label);
    gtk_widget_set_valign(b, GTK_ALIGN_CENTER);
    g_signal_connect(b, "clicked", cb, data);
    adw_action_row_add_suffix(ADW_ACTION_ROW(row), b);
    return b;
}

static GtkWidget *action_row(const char *title, const char *subtitle, const char *icon) {
    GtkWidget *row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    if (subtitle != NULL) {
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle);
    }
    if (icon != NULL) {
        adw_action_row_add_prefix(ADW_ACTION_ROW(row), gtk_image_new_from_icon_name(icon));
    }
    return row;
}

GtkWidget *mi_device_page_new(AppState *state, MiwearDashboardUi *dash) {
    GtkWidget *page = adw_preferences_page_new();

    /* Hero */
    GtkWidget *hero_group = adw_preferences_group_new();
    GtkWidget *hero = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_top(hero, 6);
    GtkWidget *icon = gtk_image_new_from_icon_name("miwear-watch-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 48);
    gtk_widget_add_css_class(icon, "mi-band-icon");
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
    dash->dev_name = mi_label("Your band", "title-2", 0.5f);
    dash->dev_address = mi_label("", "caption dimmed numeric", 0.5f);
    dash->dev_state = mi_label("", "dimmed", 0.5f);
    gtk_label_set_wrap(GTK_LABEL(dash->dev_state), TRUE);
    gtk_label_set_justify(GTK_LABEL(dash->dev_state), GTK_JUSTIFY_CENTER);
    dash->dev_connect = gtk_button_new_with_label("Connect");
    gtk_widget_add_css_class(dash->dev_connect, "pill");
    gtk_widget_set_halign(dash->dev_connect, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(dash->dev_connect, 10);
    g_signal_connect(dash->dev_connect, "clicked", G_CALLBACK(on_connect_clicked), state);
    gtk_box_append(GTK_BOX(hero), icon);
    gtk_box_append(GTK_BOX(hero), dash->dev_name);
    gtk_box_append(GTK_BOX(hero), dash->dev_address);
    gtk_box_append(GTK_BOX(hero), dash->dev_state);
    gtk_box_append(GTK_BOX(hero), dash->dev_connect);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(hero_group), hero);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(hero_group));

    /* Status */
    GtkWidget *status = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(status), "Status");
    dash->dev_battery_row = action_row("Battery", NULL, NULL);
    GtkWidget *batt_icon = gtk_image_new_from_icon_name("battery-missing-symbolic");
    adw_action_row_add_prefix(ADW_ACTION_ROW(dash->dev_battery_row), batt_icon);
    g_object_set_data(G_OBJECT(dash->dev_battery_row), "mi-icon", batt_icon);
    dash->dev_battery_bar = gtk_level_bar_new_for_interval(0, 100);
    gtk_widget_set_size_request(dash->dev_battery_bar, 120, -1);
    gtk_widget_set_valign(dash->dev_battery_bar, GTK_ALIGN_CENTER);
    dash->dev_battery_label = mi_label("—", "numeric", 1.0f);
    gtk_widget_set_size_request(dash->dev_battery_label, 42, -1);
    adw_action_row_add_suffix(ADW_ACTION_ROW(dash->dev_battery_row), dash->dev_battery_bar);
    adw_action_row_add_suffix(ADW_ACTION_ROW(dash->dev_battery_row), dash->dev_battery_label);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(status), dash->dev_battery_row);
    dash->dev_status_row = action_row("Band state", "Unknown until connected", "miwear-watch-symbolic");
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(status), dash->dev_status_row);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(status));

    /* Actions */
    GtkWidget *actions = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(actions), "Actions");
    GtkWidget *row = action_row("Sync Health Data", "Fetch new activity, sleep and workouts",
                                "view-refresh-symbolic");
    dash->dev_sync_btn = row_button(row, "Sync", G_CALLBACK(on_sync_clicked), state);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(actions), row);
    row = action_row("Find Band", "Make the band vibrate", "find-location-symbolic");
    dash->dev_find_btn = row_button(row, "Ring", G_CALLBACK(on_find_clicked), state);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(actions), row);
    dash->dev_weather_row = action_row("Weather", "Not synced yet", "weather-few-clouds-symbolic");
    dash->dev_weather_btn = row_button(dash->dev_weather_row, "Send", G_CALLBACK(on_weather_clicked), state);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(actions), dash->dev_weather_row);
    dash->dev_find_phone_row = action_row("Band Is Looking for This Computer", "Dismiss on the band or stop here",
                                          "audio-volume-high-symbolic");
    GtkWidget *stop = row_button(dash->dev_find_phone_row, "Stop", G_CALLBACK(on_find_phone_stop), state);
    gtk_widget_add_css_class(stop, "destructive-action");
    gtk_widget_set_visible(dash->dev_find_phone_row, FALSE);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(actions), dash->dev_find_phone_row);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(actions));

    /* Notifications */
    GtkWidget *notif = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(notif), "Notifications");
    dash->dev_notif_switch = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(dash->dev_notif_switch), "Forward Desktop Notifications");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(dash->dev_notif_switch),
                                state->notification_status != NULL ? state->notification_status
                                                                   : "Show this computer's notifications on the band");
    adw_switch_row_set_active(ADW_SWITCH_ROW(dash->dev_notif_switch), state->notifications_enabled);
    g_signal_connect(dash->dev_notif_switch, "notify::active", G_CALLBACK(on_notif_toggled), state);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(notif), dash->dev_notif_switch);
    row = action_row("Test Notification", "Send a sample notification to the band", NULL);
    dash->dev_notif_test = row_button(row, "Send", G_CALLBACK(on_notif_test), state);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(notif), row);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(notif));

    /* Location */
    GtkWidget *location = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(location), "Location");
    GtkWidget *share = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(share), "Share Location for Workouts");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(share),
                                "When an outdoor workout starts, send this device's location to the band, like "
                                "the phone app does. Needs a GPS receiver; otherwise the band uses its own GPS");
    adw_switch_row_set_active(ADW_SWITCH_ROW(share), state->share_location_with_band);
    g_signal_connect(share, "notify::active", G_CALLBACK(on_share_location_toggled), state);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(location), share);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(location));

    /* Profile (read-only; edited in config.ini) */
    GtkWidget *profile = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(profile), "Profile");
    adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(profile),
                                          "Sent to the band on connect. Edit in ~/.config/miwear-band/config.ini.");
    static const char *titles[5] = {"Height", "Weight", "Birthday", "Max Heart Rate", "Activity Goal"};
    for (int i = 0; i < 5; i++) {
        dash->dev_profile_rows[i] = action_row(titles[i], NULL, NULL);
        gtk_widget_add_css_class(dash->dev_profile_rows[i], "property");
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(profile), dash->dev_profile_rows[i]);
    }
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(profile));

    /* Advanced */
    GtkWidget *adv = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(adv), "Advanced");
    row = action_row("Change Device", "Pair a different band or update the auth key", "bluetooth-symbolic");
    adw_action_row_add_suffix(ADW_ACTION_ROW(row), gtk_image_new_from_icon_name("go-next-symbolic"));
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), TRUE);
    g_signal_connect(row, "activated", G_CALLBACK(on_change_device), state);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(adv), row);
    row = action_row("Connection Log", "Protocol messages for troubleshooting", "utilities-terminal-symbolic");
    adw_action_row_add_suffix(ADW_ACTION_ROW(row), gtk_image_new_from_icon_name("go-next-symbolic"));
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), TRUE);
    g_signal_connect(row, "activated", G_CALLBACK(on_show_log), state);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(adv), row);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(adv));
    return page;
}

static const char *battery_icon(int pct, gboolean charging) {
    static char buf[64];
    const int level = CLAMP((pct + 5) / 10 * 10, 0, 100);
    if (charging && level == 100) {
        return "battery-level-100-charged-symbolic";
    }
    g_snprintf(buf, sizeof(buf), "battery-level-%d%s-symbolic", level, charging ? "-charging" : "");
    return buf;
}

void mi_device_refresh(AppState *state) {
    MiwearDashboardUi *dash = state->dash_ui;
    if (dash == NULL || dash->dev_name == NULL) {
        return;
    }
    const gboolean connected = mi_is_connected(state);
    const gboolean have_device = miwear_ui_has_saved_device(state);
    gtk_label_set_text(GTK_LABEL(dash->dev_name),
                       state->device_display_name != NULL && state->device_display_name[0] != '\0'
                           ? state->device_display_name
                           : (have_device ? state->selected_address : "No band set up"));
    gtk_label_set_text(GTK_LABEL(dash->dev_address), have_device ? state->selected_address : "");
    gtk_widget_set_visible(dash->dev_address,
                           have_device && g_strcmp0(state->device_display_name, state->selected_address) != 0);

    const char *state_text = connected   ? "Connected"
                             : dash->busy ? (dash->status_text != NULL ? dash->status_text : "Connecting…")
                                          : "Not connected";
    gtk_label_set_text(GTK_LABEL(dash->dev_state), state_text);
    gtk_button_set_label(GTK_BUTTON(dash->dev_connect), connected ? "Disconnect" : have_device ? "Connect" : "Set Up");
    gtk_widget_set_sensitive(dash->dev_connect, !dash->busy);
    if (connected) {
        gtk_widget_remove_css_class(dash->dev_connect, "suggested-action");
    } else {
        gtk_widget_add_css_class(dash->dev_connect, "suggested-action");
    }

    const gboolean charging = state->device_status_charging != NULL;
    if (dash->battery >= 0) {
        gchar b[16];
        g_snprintf(b, sizeof(b), "%d%%", dash->battery);
        gtk_label_set_text(GTK_LABEL(dash->dev_battery_label), b);
        gtk_level_bar_set_value(GTK_LEVEL_BAR(dash->dev_battery_bar), dash->battery);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(dash->dev_battery_row), charging ? "Charging" : "");
        gtk_image_set_from_icon_name(g_object_get_data(G_OBJECT(dash->dev_battery_row), "mi-icon"),
                                     battery_icon(dash->battery, charging));
    } else {
        gtk_label_set_text(GTK_LABEL(dash->dev_battery_label), "—");
        gtk_level_bar_set_value(GTK_LEVEL_BAR(dash->dev_battery_bar), 0);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(dash->dev_battery_row),
                                    connected ? "Waiting for the band" : "Known after connecting");
    }

    GString *st = g_string_new(NULL);
    const char *parts[3] = {state->device_status_wearing, state->device_status_charging, state->device_status_sleep};
    for (int i = 0; i < 3; i++) {
        if (parts[i] != NULL && parts[i][0] != '\0') {
            if (st->len) {
                g_string_append(st, " · ");
            }
            g_string_append(st, parts[i]);
        }
    }
    adw_action_row_set_subtitle(ADW_ACTION_ROW(dash->dev_status_row),
                                st->len ? st->str : (connected ? "No state reported yet" : "Unknown until connected"));
    g_string_free(st, TRUE);

    gtk_widget_set_sensitive(dash->dev_sync_btn, connected);
    gtk_widget_set_sensitive(dash->dev_find_btn, connected);
    gtk_widget_set_sensitive(dash->dev_weather_btn, connected);
    gtk_widget_set_sensitive(dash->dev_notif_test, connected);
    adw_action_row_set_subtitle(ADW_ACTION_ROW(dash->dev_weather_row),
                                state->weather_last_status != NULL ? state->weather_last_status
                                                                   : "Send the current forecast to the band");
    gtk_widget_set_visible(dash->dev_find_phone_row, miwear_find_phone_is_active(state));
    if (adw_switch_row_get_active(ADW_SWITCH_ROW(dash->dev_notif_switch)) != state->notifications_enabled) {
        adw_switch_row_set_active(ADW_SWITCH_ROW(dash->dev_notif_switch), state->notifications_enabled);
    }

    gchar v[64];
    g_snprintf(v, sizeof(v), state->profile_height_cm > 0 ? "%u cm" : "—", state->profile_height_cm);
    adw_action_row_set_subtitle(ADW_ACTION_ROW(dash->dev_profile_rows[0]), v);
    g_snprintf(v, sizeof(v), state->profile_weight_kg > 0 ? "%.1f kg" : "—", (double)state->profile_weight_kg);
    adw_action_row_set_subtitle(ADW_ACTION_ROW(dash->dev_profile_rows[1]), v);
    if (state->profile_birthday_yyyymmdd > 19000000u) {
        g_snprintf(v, sizeof(v), "%04u-%02u-%02u", state->profile_birthday_yyyymmdd / 10000u,
                   (state->profile_birthday_yyyymmdd / 100u) % 100u, state->profile_birthday_yyyymmdd % 100u);
    } else {
        g_strlcpy(v, "—", sizeof(v));
    }
    adw_action_row_set_subtitle(ADW_ACTION_ROW(dash->dev_profile_rows[2]), v);
    g_snprintf(v, sizeof(v), state->profile_max_hr > 0 ? "%u bpm" : "—", state->profile_max_hr);
    adw_action_row_set_subtitle(ADW_ACTION_ROW(dash->dev_profile_rows[3]), v);
    g_snprintf(v, sizeof(v), "%u active minutes a day",
               state->goal_moving > 0 ? state->goal_moving : miwear_activity_goal_default());
    adw_action_row_set_subtitle(ADW_ACTION_ROW(dash->dev_profile_rows[4]), v);
}
