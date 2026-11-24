#include "ui_private.h"

#include "salus/config.h"
#include "salus/services/find_phone.h"
#include "salus/services/notifications.h"
#include "salus/services/weather.h"
#include "salus/services/location.h"

#include <string.h>

static void on_connect_clicked(GtkButton *b, gpointer user_data) {
    (void)b;
    SalusApp *state = user_data;
    if (ui_is_connected(state)) {
        salus_device_disconnect(state);
        ui_toast(state, "Disconnected");
    } else {
        ui_connect_saved(state);
    }
}

static void on_sync_clicked(GtkButton *b, gpointer user_data) {
    (void)b;
    SalusApp *state = user_data;
    salus_device_sync(state);
    ui_toast(state, "Syncing health data…");
}

static void on_find_clicked(GtkButton *b, gpointer user_data) {
    (void)b;
    SalusApp *state = user_data;
    ui_toast(state, salus_device_find(state, TRUE) ? "Your device is vibrating" : "Couldn't reach the device");
}

static void on_weather_clicked(GtkButton *b, gpointer user_data) {
    (void)b;
    salus_weather_refresh(user_data);
    ui_toast(user_data, "Sending weather to the band…");
}

static void on_find_phone_stop(GtkButton *b, gpointer user_data) {
    (void)b;
    salus_find_phone_dismiss(user_data);
}

static void on_notif_toggled(GObject *obj, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    SalusApp *state = user_data;
    const gboolean on = adw_switch_row_get_active(ADW_SWITCH_ROW(obj));
    if (on != state->notifications_enabled) {
        salus_notifications_set_enabled(state, on);
    }
}

static void on_share_location_toggled(GObject *obj, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    SalusApp *state = user_data;
    const gboolean on = adw_switch_row_get_active(ADW_SWITCH_ROW(obj));
    if (on == state->share_location_with_band) {
        return;
    }
    state->share_location_with_band = on;
    (void)salus_config_save(state, NULL);
    if (!on) {
        salus_location_stop(state, "location sharing turned off");
    }
}

static void on_notif_test(GtkButton *b, gpointer user_data) {
    (void)b;
    salus_notifications_send_test(user_data);
}

static void on_add_device(AdwActionRow *row, gpointer user_data) {
    (void)row;
    salus_ui_show_setup(user_data);
}

static void on_saved_device_activated(AdwActionRow *row, gpointer user_data) {
    SalusApp *state = user_data;
    const char *address = g_object_get_data(G_OBJECT(row), "salus-address");
    if (state->device != NULL && g_ascii_strcasecmp(state->device->config->address, address) == 0) {
        return;
    }
    salus_app_activate_device(state, address);
    (void)salus_config_save(state, NULL);
    salus_ui_reload_data(state);
    salus_device_connect(state);
}

static void on_forget_response(AdwAlertDialog *dialog, const char *response, gpointer user_data) {
    SalusApp *state = user_data;
    if (g_strcmp0(response, "forget") != 0) {
        return;
    }
    const char *address = g_object_get_data(G_OBJECT(dialog), "salus-address");
    salus_app_forget_device(state, address);
    (void)salus_config_save(state, NULL);
    if (state->device == NULL) {
        salus_ui_show_setup(state);
    } else {
        salus_ui_reload_data(state);
    }
    salus_dashboard_refresh(state);
}

static void on_forget_clicked(GtkButton *b, gpointer user_data) {
    SalusApp *state = user_data;
    const char *address = g_object_get_data(G_OBJECT(b), "salus-address");
    const char *name = g_object_get_data(G_OBJECT(b), "salus-name");
    gchar *heading = g_strdup_printf("Forget %s?", name);
    AdwDialog *dialog = adw_alert_dialog_new(heading, "Its pairing details are removed from Salus. Health data "
                                                      "already synced from it stays.");
    g_free(heading);
    g_object_set_data_full(G_OBJECT(dialog), "salus-address", g_strdup(address), g_free);
    adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(dialog), "cancel", "_Cancel", "forget", "_Forget", NULL);
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog), "forget", ADW_RESPONSE_DESTRUCTIVE);
    g_signal_connect(dialog, "response", G_CALLBACK(on_forget_response), state);
    adw_dialog_present(dialog, state->window);
}

/* Saved devices: one row each (switch on activate, forget button), then "Add Device…". */
static void rebuild_saved_devices(SalusApp *state) {
    SalusDashboardUi *dash = state->dash_ui;
    GtkWidget *group = dash->dev_saved_group;
    if (dash->dev_saved_rows == NULL) {
        dash->dev_saved_rows = g_ptr_array_new();
    }
    for (guint i = 0; i < dash->dev_saved_rows->len; i++) {
        adw_preferences_group_remove(ADW_PREFERENCES_GROUP(group), g_ptr_array_index(dash->dev_saved_rows, i));
    }
    g_ptr_array_set_size(dash->dev_saved_rows, 0);
    for (guint i = 0; state->saved_devices != NULL && i < state->saved_devices->len; i++) {
        const SalusDeviceConfig *cfg = g_ptr_array_index(state->saved_devices, i);
        const SalusDriver *driver = salus_driver_find(cfg->driver_id);
        const gboolean active =
            state->device != NULL && g_ascii_strcasecmp(state->device->config->address, cfg->address) == 0;
        gchar *name = salus_device_config_display_name(cfg);
        gchar *sub = g_strdup_printf("%s %s · %s", driver != NULL ? driver->vendor : "Unknown",
                                     driver != NULL ? driver->model : cfg->driver_id, cfg->address);
        GtkWidget *row = adw_action_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), name);
        adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row), FALSE);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), sub);
        adw_action_row_add_prefix(ADW_ACTION_ROW(row),
                                  gtk_image_new_from_icon_name(driver != NULL ? driver->icon : "bluetooth-symbolic"));
        if (active) {
            GtkWidget *check = gtk_image_new_from_icon_name("object-select-symbolic");
            gtk_widget_set_tooltip_text(check, "Active device");
            adw_action_row_add_suffix(ADW_ACTION_ROW(row), check);
        } else {
            gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), TRUE);
            gtk_widget_set_tooltip_text(row, "Use this device");
            g_object_set_data_full(G_OBJECT(row), "salus-address", g_strdup(cfg->address), g_free);
            g_signal_connect(row, "activated", G_CALLBACK(on_saved_device_activated), state);
        }
        GtkWidget *forget = gtk_button_new_from_icon_name("user-trash-symbolic");
        gtk_widget_add_css_class(forget, "flat");
        gtk_widget_set_valign(forget, GTK_ALIGN_CENTER);
        gtk_widget_set_tooltip_text(forget, "Forget device");
        g_object_set_data_full(G_OBJECT(forget), "salus-address", g_strdup(cfg->address), g_free);
        g_object_set_data_full(G_OBJECT(forget), "salus-name", g_strdup(name), g_free);
        g_signal_connect(forget, "clicked", G_CALLBACK(on_forget_clicked), state);
        adw_action_row_add_suffix(ADW_ACTION_ROW(row), forget);
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), row);
        g_ptr_array_add(dash->dev_saved_rows, row);
        g_free(name);
        g_free(sub);
    }
    GtkWidget *add = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(add), "Add Device…");
    adw_action_row_add_prefix(ADW_ACTION_ROW(add), gtk_image_new_from_icon_name("list-add-symbolic"));
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(add), TRUE);
    g_signal_connect(add, "activated", G_CALLBACK(on_add_device), state);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), add);
    g_ptr_array_add(dash->dev_saved_rows, add);
}

static void on_show_log(AdwActionRow *row, gpointer user_data) {
    (void)row;
    ui_show_log_dialog(user_data);
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

GtkWidget *ui_device_page_new(SalusApp *state, SalusDashboardUi *dash) {
    GtkWidget *page = adw_preferences_page_new();

    /* Hero */
    GtkWidget *hero_group = adw_preferences_group_new();
    GtkWidget *hero = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_top(hero, 6);
    GtkWidget *icon = gtk_image_new_from_icon_name("salus-watch-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 48);
    gtk_widget_add_css_class(icon, "salus-band-icon");
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
    dash->dev_icon = icon;
    dash->dev_name = ui_label("Your device", "title-2", 0.5f);
    dash->dev_model = ui_label("", "dimmed", 0.5f);
    dash->dev_address = ui_label("", "caption dimmed numeric", 0.5f);
    dash->dev_state = ui_label("", "dimmed", 0.5f);
    gtk_label_set_wrap(GTK_LABEL(dash->dev_state), TRUE);
    gtk_label_set_justify(GTK_LABEL(dash->dev_state), GTK_JUSTIFY_CENTER);
    dash->dev_connect = gtk_button_new_with_label("Connect");
    gtk_widget_add_css_class(dash->dev_connect, "pill");
    gtk_widget_set_halign(dash->dev_connect, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(dash->dev_connect, 10);
    g_signal_connect(dash->dev_connect, "clicked", G_CALLBACK(on_connect_clicked), state);
    gtk_box_append(GTK_BOX(hero), icon);
    gtk_box_append(GTK_BOX(hero), dash->dev_name);
    gtk_box_append(GTK_BOX(hero), dash->dev_model);
    gtk_box_append(GTK_BOX(hero), dash->dev_address);
    gtk_box_append(GTK_BOX(hero), dash->dev_state);
    gtk_box_append(GTK_BOX(hero), dash->dev_connect);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(hero_group), hero);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(hero_group));

    dash->dev_saved_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(dash->dev_saved_group), "Saved Devices");
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(dash->dev_saved_group));

    /* Status */
    GtkWidget *status = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(status), "Status");
    dash->dev_battery_row = action_row("Battery", NULL, NULL);
    GtkWidget *batt_icon = gtk_image_new_from_icon_name("battery-missing-symbolic");
    adw_action_row_add_prefix(ADW_ACTION_ROW(dash->dev_battery_row), batt_icon);
    g_object_set_data(G_OBJECT(dash->dev_battery_row), "salus-icon", batt_icon);
    dash->dev_battery_bar = gtk_level_bar_new_for_interval(0, 100);
    gtk_widget_set_size_request(dash->dev_battery_bar, 120, -1);
    gtk_widget_set_valign(dash->dev_battery_bar, GTK_ALIGN_CENTER);
    dash->dev_battery_label = ui_label("—", "numeric", 1.0f);
    gtk_widget_set_size_request(dash->dev_battery_label, 42, -1);
    adw_action_row_add_suffix(ADW_ACTION_ROW(dash->dev_battery_row), dash->dev_battery_bar);
    adw_action_row_add_suffix(ADW_ACTION_ROW(dash->dev_battery_row), dash->dev_battery_label);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(status), dash->dev_battery_row);
    dash->dev_status_row = action_row("Device State", "Unknown until connected", "salus-watch-symbolic");
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(status), dash->dev_status_row);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(status));

    /* Actions */
    GtkWidget *actions = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(actions), "Actions");
    GtkWidget *row = action_row("Sync Health Data", "Fetch new activity, sleep and workouts",
                                "view-refresh-symbolic");
    dash->dev_sync_btn = row_button(row, "Sync", G_CALLBACK(on_sync_clicked), state);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(actions), row);
    dash->dev_find_row = action_row("Find Device", "Make it ring or vibrate", "find-location-symbolic");
    dash->dev_find_btn = row_button(dash->dev_find_row, "Ring", G_CALLBACK(on_find_clicked), state);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(actions), dash->dev_find_row);
    dash->dev_weather_row = action_row("Weather", "Not synced yet", "weather-few-clouds-symbolic");
    dash->dev_weather_btn = row_button(dash->dev_weather_row, "Send", G_CALLBACK(on_weather_clicked), state);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(actions), dash->dev_weather_row);
    dash->dev_find_phone_row = action_row("Your Device Is Looking for This Computer", "Dismiss it on the device or stop here",
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
                                                                   : "Show this computer's notifications on your device");
    adw_switch_row_set_active(ADW_SWITCH_ROW(dash->dev_notif_switch), state->notifications_enabled);
    g_signal_connect(dash->dev_notif_switch, "notify::active", G_CALLBACK(on_notif_toggled), state);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(notif), dash->dev_notif_switch);
    row = action_row("Test Notification", "Send a sample notification to your device", NULL);
    dash->dev_notif_test = row_button(row, "Send", G_CALLBACK(on_notif_test), state);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(notif), row);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(notif));
    dash->dev_notif_group = notif;

    /* Location */
    GtkWidget *location = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(location), "Location");
    GtkWidget *share = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(share), "Share Location for Workouts");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(share),
                                "When an outdoor workout starts, send this computer's location to your device, like "
                                "a phone app does. Needs a GPS receiver; otherwise the device uses its own GPS");
    adw_switch_row_set_active(ADW_SWITCH_ROW(share), state->share_location_with_band);
    g_signal_connect(share, "notify::active", G_CALLBACK(on_share_location_toggled), state);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(location), share);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(location));
    dash->dev_location_group = location;

    /* Profile (read-only; edited in config.ini) */
    GtkWidget *profile = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(profile), "Profile");
    adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(profile),
                                          "Sent to your device on connect. Edit in ~/.config/salus/config.ini.");
    static const char *titles[5] = {"Height", "Weight", "Birthday", "Max Heart Rate", "Activity Goal"};
    for (int i = 0; i < 5; i++) {
        dash->dev_profile_rows[i] = action_row(titles[i], NULL, NULL);
        gtk_widget_add_css_class(dash->dev_profile_rows[i], "property");
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(profile), dash->dev_profile_rows[i]);
    }
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(profile));
    dash->dev_profile_group = profile;

    /* Advanced */
    GtkWidget *adv = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(adv), "Advanced");
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

void ui_device_refresh(SalusApp *state) {
    SalusDashboardUi *dash = state->dash_ui;
    if (dash == NULL || dash->dev_name == NULL) {
        return;
    }
    const gboolean connected = ui_is_connected(state);
    const gboolean have_device = state->device != NULL;
    const SalusDriver *driver = have_device ? state->device->driver : NULL;
    gtk_label_set_text(GTK_LABEL(dash->dev_name),
                       state->device_display_name != NULL && state->device_display_name[0] != '\0'
                           ? state->device_display_name
                           : (have_device ? state->selected_address : "No device set up"));
    gchar *model = driver != NULL ? g_strdup_printf("%s %s", driver->vendor, driver->model) : g_strdup("");
    gtk_label_set_text(GTK_LABEL(dash->dev_model), model);
    gtk_widget_set_visible(dash->dev_model, driver != NULL);
    g_free(model);
    gtk_image_set_from_icon_name(GTK_IMAGE(dash->dev_icon), driver != NULL ? driver->icon : "salus-watch-symbolic");
    rebuild_saved_devices(state);

    /* Only what the active device supports. */
    const guint caps = driver != NULL ? driver->caps : 0;
    gtk_widget_set_visible(dash->dev_battery_row, (caps & SALUS_CAP_BATTERY) != 0);
    gtk_widget_set_visible(dash->dev_find_row, (caps & SALUS_CAP_FIND_DEVICE) != 0);
    gtk_widget_set_visible(dash->dev_weather_row, (caps & SALUS_CAP_WEATHER) != 0);
    gtk_widget_set_visible(dash->dev_notif_group, (caps & SALUS_CAP_NOTIFICATIONS) != 0);
    gtk_widget_set_visible(dash->dev_location_group, (caps & SALUS_CAP_LOCATION_SHARE) != 0);
    gtk_widget_set_visible(dash->dev_profile_group, (caps & SALUS_CAP_USER_PROFILE) != 0);
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
        gtk_image_set_from_icon_name(g_object_get_data(G_OBJECT(dash->dev_battery_row), "salus-icon"),
                                     battery_icon(dash->battery, charging));
    } else {
        gtk_label_set_text(GTK_LABEL(dash->dev_battery_label), "—");
        gtk_level_bar_set_value(GTK_LEVEL_BAR(dash->dev_battery_bar), 0);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(dash->dev_battery_row),
                                    connected ? "Waiting for the device" : "Known after connecting");
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
                                                                   : "Send the current forecast to your device");
    gtk_widget_set_visible(dash->dev_find_phone_row, salus_find_phone_is_active(state));
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
               state->goal_moving > 0 ? state->goal_moving : salus_activity_goal_default());
    adw_action_row_set_subtitle(ADW_ACTION_ROW(dash->dev_profile_rows[4]), v);
}
