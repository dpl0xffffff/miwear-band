#include "ui_private.h"

#include "salus/config.h"

#include <string.h>

/*
 * Connect flow: (1) pick the kind of device from the driver registry, (2) pick it from BlueZ's
 * Bluetooth devices, (3) fill in what the driver needs to pair (e.g. an auth key) and connect.
 * The device is saved once it has connected (salus_device_set_link).
 */

struct SalusConnectUi {
    SalusApp *app;
    GtkWidget *nav;           /* AdwNavigationView */
    GPtrArray *cancel_buttons; /* one per page, visible when there is a saved device to go back to */
    gchar *prev_active;       /* active device when the flow was opened (restored on cancel) */
    const SalusDriver *driver;

    /* Bluetooth page */
    AdwNavigationPage *bt_page;
    GtkWidget *bt_intro;
    GtkWidget *bt_list;
    GtkWidget *bt_show_all;
    GtkWidget *bt_next;
    GtkWidget *bt_refresh;
    gchar *bt_selected;
    gchar *bt_selected_name;
    gboolean bt_populating;

    /* Pairing page */
    AdwNavigationPage *pair_page;
    GtkWidget *pair_help;
    GtkWidget *pair_group;
    GPtrArray *field_rows;    /* entry rows, in driver->fields order */
    GtkWidget *pair_connect;
    GtkWidget *pair_spinner;
    GtkWidget *pair_status;
    gboolean busy;
};

static SalusConnectUi *cui(SalusApp *app) {
    return app->connect_ui;
}

/* ---- Helpers ---------------------------------------------------------------------------- */

static GtkWidget *page_header(SalusConnectUi *c) {
    GtkWidget *header = adw_header_bar_new();
    GtkWidget *cancel = gtk_button_new_with_label("Cancel");
    g_signal_connect_swapped(cancel, "clicked", G_CALLBACK(salus_connect_cancel), c->app);
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), cancel);
    g_ptr_array_add(c->cancel_buttons, cancel);
    return header;
}

static AdwNavigationPage *make_page(SalusConnectUi *c, GtkWidget *content, const char *title, const char *tag) {
    GtkWidget *tb = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(tb), page_header(c));
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(tb), ui_page_scroll(content, 560));
    return adw_navigation_page_new_with_tag(tb, title, tag);
}

static GtkWidget *intro(const char *icon, const char *title, const char *text, GtkWidget **text_out) {
    GtkWidget *head = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *img = gtk_image_new_from_icon_name(icon);
    gtk_image_set_pixel_size(GTK_IMAGE(img), 56);
    gtk_widget_add_css_class(img, "salus-band-icon");
    gtk_widget_set_halign(img, GTK_ALIGN_CENTER);
    GtkWidget *t = gtk_label_new(title);
    gtk_widget_add_css_class(t, "title-1");
    gtk_label_set_wrap(GTK_LABEL(t), TRUE);
    gtk_label_set_justify(GTK_LABEL(t), GTK_JUSTIFY_CENTER);
    GtkWidget *d = gtk_label_new(text);
    gtk_widget_add_css_class(d, "dimmed");
    gtk_label_set_wrap(GTK_LABEL(d), TRUE);
    gtk_label_set_justify(GTK_LABEL(d), GTK_JUSTIFY_CENTER);
    gtk_box_append(GTK_BOX(head), img);
    gtk_box_append(GTK_BOX(head), t);
    gtk_box_append(GTK_BOX(head), d);
    if (text_out != NULL) {
        *text_out = d;
    }
    return head;
}

static gchar *driver_title(const SalusDriver *d) {
    return g_strdup_printf("%s %s", d->vendor, d->model);
}

/* "Heart rate, SpO₂, sleep, workouts, notifications" */
static gchar *caps_summary(guint caps) {
    static const struct {
        guint cap;
        const char *name;
    } names[] = {
        {SALUS_CAP_HEART_RATE, "heart rate"}, {SALUS_CAP_SPO2, "SpO₂"},         {SALUS_CAP_STRESS, "stress"},
        {SALUS_CAP_STEPS, "activity"},        {SALUS_CAP_SLEEP, "sleep"},       {SALUS_CAP_WORKOUTS, "workouts"},
        {SALUS_CAP_NOTIFICATIONS, "notifications"},
    };
    GString *s = g_string_new(NULL);
    for (size_t i = 0; i < G_N_ELEMENTS(names); i++) {
        if (caps & names[i].cap) {
            g_string_append_printf(s, "%s%s", s->len ? ", " : "", names[i].name);
        }
    }
    if (s->len > 0) {
        s->str[0] = g_ascii_toupper(s->str[0]);
    }
    return g_string_free(s, FALSE);
}

/* ---- Pairing page ----------------------------------------------------------------------- */

static void pair_update_sensitivity(SalusConnectUi *c) {
    gboolean ok = c->driver != NULL && c->bt_selected != NULL && !c->busy;
    for (guint i = 0; c->driver != NULL && i < c->field_rows->len; i++) {
        GtkWidget *row = g_ptr_array_index(c->field_rows, i);
        const SalusPairingField *f = &c->driver->fields[i];
        const char *v = gtk_editable_get_text(GTK_EDITABLE(row));
        const gboolean valid = f->validate == NULL || f->validate(v);
        if (v != NULL && v[0] != '\0' && !valid) {
            gtk_widget_add_css_class(row, "error");
        } else {
            gtk_widget_remove_css_class(row, "error");
        }
        ok = ok && valid;
    }
    gtk_widget_set_sensitive(c->pair_connect, ok);
}

static void on_field_changed(GtkEditable *e, gpointer user_data) {
    (void)e;
    pair_update_sensitivity(user_data);
}

static void on_connect_clicked(GtkButton *b, gpointer user_data) {
    (void)b;
    SalusConnectUi *c = user_data;
    SalusDeviceConfig *cfg = salus_device_config_new(c->bt_selected, c->driver->id);
    cfg->name = g_strdup(c->bt_selected_name);
    for (guint i = 0; i < c->field_rows->len; i++) {
        const char *v = gtk_editable_get_text(GTK_EDITABLE(g_ptr_array_index(c->field_rows, i)));
        gchar *trimmed = g_strstrip(g_strdup(v));
        salus_device_config_set(cfg, c->driver->fields[i].key, trimmed);
        g_free(trimmed);
    }
    salus_app_set_device(c->app, cfg); /* saved once it connects */
    salus_dashboard_update_device_state(c->app, NULL, NULL, NULL);
    salus_device_connect(c->app);
}

static void show_pair_page(SalusConnectUi *c) {
    for (guint i = 0; i < c->field_rows->len; i++) {
        adw_preferences_group_remove(ADW_PREFERENCES_GROUP(c->pair_group), g_ptr_array_index(c->field_rows, i));
    }
    g_ptr_array_set_size(c->field_rows, 0);
    /* Prefill from the saved device at this address (e.g. after a wrong key). */
    const SalusDeviceConfig *saved = salus_app_saved_device(c->app, c->bt_selected);
    const SalusDeviceConfig *prefill =
        saved != NULL && g_strcmp0(saved->driver_id, c->driver->id) == 0 ? saved : NULL;
    if (prefill == NULL && c->app->device != NULL && g_strcmp0(c->app->device->config->address, c->bt_selected) == 0) {
        prefill = c->app->device->config;
    }
    GString *desc = g_string_new(NULL);
    for (guint i = 0; i < c->driver->n_fields; i++) {
        const SalusPairingField *f = &c->driver->fields[i];
        GtkWidget *row = f->secret ? adw_password_entry_row_new() : adw_entry_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), f->title);
        const char *v = salus_device_config_get(prefill, f->key);
        gtk_editable_set_text(GTK_EDITABLE(row), v != NULL ? v : "");
        g_signal_connect(row, "changed", G_CALLBACK(on_field_changed), c);
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(c->pair_group), row);
        g_ptr_array_add(c->field_rows, row);
        if (f->help != NULL) {
            g_string_append_printf(desc, "%s%s", desc->len ? "\n" : "", f->help);
        }
    }
    adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(c->pair_group), desc->str);
    gtk_widget_set_visible(c->pair_group, c->driver->n_fields > 0);
    g_string_free(desc, TRUE);
    gtk_label_set_text(GTK_LABEL(c->pair_help),
                       c->driver->pairing_help != NULL ? c->driver->pairing_help : "Ready to connect.");
    gtk_label_set_text(GTK_LABEL(c->pair_status), "");
    gchar *title = g_strdup_printf("Connect %s", c->bt_selected_name != NULL ? c->bt_selected_name : c->bt_selected);
    adw_navigation_page_set_title(c->pair_page, title);
    g_free(title);
    pair_update_sensitivity(c);
    adw_navigation_view_push(ADW_NAVIGATION_VIEW(c->nav), c->pair_page);
}

static AdwNavigationPage *build_pair_page(SalusConnectUi *c) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 24);
    gtk_widget_add_css_class(box, "salus-page");
    gtk_box_append(GTK_BOX(box), intro("salus-watch-symbolic", "Pair Your Device", "", &c->pair_help));
    c->pair_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(c->pair_group), "Pairing");
    gtk_box_append(GTK_BOX(box), c->pair_group);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_halign(row, GTK_ALIGN_CENTER);
    c->pair_connect = gtk_button_new_with_label("Connect");
    gtk_widget_add_css_class(c->pair_connect, "pill");
    gtk_widget_add_css_class(c->pair_connect, "suggested-action");
    g_signal_connect(c->pair_connect, "clicked", G_CALLBACK(on_connect_clicked), c);
    c->pair_spinner = adw_spinner_new();
    gtk_widget_set_visible(c->pair_spinner, FALSE);
    gtk_box_append(GTK_BOX(row), c->pair_connect);
    gtk_box_append(GTK_BOX(row), c->pair_spinner);
    c->pair_status = gtk_label_new("");
    gtk_widget_add_css_class(c->pair_status, "dimmed");
    gtk_label_set_wrap(GTK_LABEL(c->pair_status), TRUE);
    gtk_label_set_justify(GTK_LABEL(c->pair_status), GTK_JUSTIFY_CENTER);
    gtk_box_append(GTK_BOX(actions), row);
    gtk_box_append(GTK_BOX(actions), c->pair_status);
    gtk_box_append(GTK_BOX(box), actions);
    return make_page(c, box, "Connect", "pair");
}

/* ---- Bluetooth page --------------------------------------------------------------------- */

static gboolean name_matches_driver(const SalusDriver *d, const char *name) {
    for (const char *const *p = d->bt_name_patterns; p != NULL && *p != NULL && name != NULL; p++) {
        if (g_pattern_match_simple(*p, name)) {
            return TRUE;
        }
    }
    return FALSE;
}

static void bt_update(SalusConnectUi *c) {
    gtk_widget_set_sensitive(c->bt_next, c->bt_selected != NULL && !c->busy);
}

static void on_bt_toggled(GtkCheckButton *check, gpointer user_data) {
    SalusConnectUi *c = user_data;
    if (!gtk_check_button_get_active(check)) {
        return;
    }
    g_free(c->bt_selected);
    g_free(c->bt_selected_name);
    c->bt_selected = g_strdup(g_object_get_data(G_OBJECT(check), "address"));
    c->bt_selected_name = g_strdup(g_object_get_data(G_OBJECT(check), "name"));
    bt_update(c);
}

static void populate_bt_list(SalusConnectUi *c);

static void on_bt_show_all(GObject *obj, GParamSpec *pspec, gpointer user_data) {
    (void)obj;
    (void)pspec;
    populate_bt_list(user_data);
}

/* Lists BlueZ's devices: the ones that look like the chosen driver's, or all of them. */
static void populate_bt_list(SalusConnectUi *c) {
    if (c->bt_populating || c->driver == NULL) {
        return;
    }
    c->bt_populating = TRUE;
    gtk_list_box_remove_all(GTK_LIST_BOX(c->bt_list));
    g_clear_pointer(&c->bt_selected, g_free);
    g_clear_pointer(&c->bt_selected_name, g_free);
    const gboolean show_all = adw_switch_row_get_active(ADW_SWITCH_ROW(c->bt_show_all));
    const char *active = c->app->device != NULL ? c->app->device->config->address : NULL;

    GError *error = NULL;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    GVariant *reply = NULL;
    if (bus != NULL) {
        reply = g_dbus_connection_call_sync(bus, "org.bluez", "/", "org.freedesktop.DBus.ObjectManager",
                                            "GetManagedObjects", NULL, G_VARIANT_TYPE("(a{oa{sa{sv}}})"),
                                            G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    }
    if (reply == NULL) {
        set_status(c->app, error != NULL ? error->message : "Couldn't list Bluetooth devices.");
        g_clear_error(&error);
        g_clear_object(&bus);
        c->bt_populating = FALSE;
        bt_update(c);
        return;
    }

    GVariant *objects = g_variant_get_child_value(reply, 0);
    GVariantIter iter;
    g_variant_iter_init(&iter, objects);
    GtkCheckButton *group = NULL, *preselect = NULL;
    guint shown = 0, hidden = 0;
    GVariant *entry;
    while ((entry = g_variant_iter_next_value(&iter)) != NULL) {
        GVariant *ifaces = NULL;
        g_variant_get(entry, "{&o@a{sa{sv}}}", NULL, &ifaces);
        GVariant *props = g_variant_lookup_value(ifaces, "org.bluez.Device1", G_VARIANT_TYPE("a{sv}"));
        if (props != NULL) {
            const gchar *address = NULL, *name = NULL, *alias = NULL;
            gboolean paired = FALSE, connected = FALSE;
            (void)g_variant_lookup(props, "Address", "&s", &address);
            (void)g_variant_lookup(props, "Name", "&s", &name);
            (void)g_variant_lookup(props, "Alias", "&s", &alias);
            (void)g_variant_lookup(props, "Paired", "b", &paired);
            (void)g_variant_lookup(props, "Connected", "b", &connected);
            const gchar *title = (name != NULL && name[0]) ? name : (alias != NULL && alias[0]) ? alias : address;
            const gboolean likely = name_matches_driver(c->driver, title) || g_strcmp0(address, active) == 0;
            if (address != NULL && (show_all || likely)) {
                GString *sub = g_string_new(address);
                if (paired) {
                    g_string_append(sub, " · Paired");
                }
                if (connected) {
                    g_string_append(sub, " · Connected");
                }
                GtkWidget *row = adw_action_row_new();
                adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
                adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row), FALSE);
                adw_action_row_set_subtitle(ADW_ACTION_ROW(row), sub->str);
                g_string_free(sub, TRUE);
                GtkWidget *check = gtk_check_button_new();
                if (group != NULL) {
                    gtk_check_button_set_group(GTK_CHECK_BUTTON(check), group);
                } else {
                    group = GTK_CHECK_BUTTON(check);
                }
                g_object_set_data_full(G_OBJECT(check), "address", g_strdup(address), g_free);
                g_object_set_data_full(G_OBJECT(check), "name", g_strdup(title), g_free);
                g_signal_connect(check, "toggled", G_CALLBACK(on_bt_toggled), c);
                adw_action_row_add_prefix(ADW_ACTION_ROW(row), check);
                adw_action_row_set_activatable_widget(ADW_ACTION_ROW(row), check);
                gtk_list_box_append(GTK_LIST_BOX(c->bt_list), row);
                if (preselect == NULL && (g_strcmp0(address, active) == 0 || likely)) {
                    preselect = GTK_CHECK_BUTTON(check);
                }
                shown++;
            } else if (address != NULL) {
                hidden++;
            }
            g_variant_unref(props);
        }
        g_variant_unref(ifaces);
        g_variant_unref(entry);
    }
    g_variant_unref(objects);
    g_variant_unref(reply);
    g_object_unref(bus);

    /* Preselects the device that is probably the right one; never connects by itself. */
    if (preselect != NULL) {
        gtk_check_button_set_active(preselect, TRUE);
    } else if (shown == 1 && group != NULL) {
        gtk_check_button_set_active(group, TRUE);
    }
    adw_switch_row_set_active(ADW_SWITCH_ROW(c->bt_show_all), show_all || (shown == 0 && hidden > 0));
    c->bt_populating = FALSE;
    if (shown == 0 && hidden > 0 && !show_all) {
        populate_bt_list(c); /* nothing looked like the device: list everything */
        return;
    }
    bt_update(c);
}

static void on_bt_next(GtkButton *b, gpointer user_data) {
    (void)b;
    show_pair_page(user_data);
}

static void on_bt_refresh(GtkButton *b, gpointer user_data) {
    (void)b;
    populate_bt_list(user_data);
}

static AdwNavigationPage *build_bt_page(SalusConnectUi *c) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 24);
    gtk_widget_add_css_class(box, "salus-page");
    gtk_box_append(GTK_BOX(box), intro("bluetooth-active-symbolic", "Choose Your Device", "", &c->bt_intro));

    GtkWidget *group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(group), "Bluetooth Devices");
    adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(group),
                                          "Pair the device in your system's Bluetooth settings if it's missing.");
    c->bt_refresh = gtk_button_new_from_icon_name("view-refresh-symbolic");
    gtk_widget_add_css_class(c->bt_refresh, "flat");
    gtk_widget_set_tooltip_text(c->bt_refresh, "Refresh device list");
    g_signal_connect(c->bt_refresh, "clicked", G_CALLBACK(on_bt_refresh), c);
    adw_preferences_group_set_header_suffix(ADW_PREFERENCES_GROUP(group), c->bt_refresh);
    c->bt_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(c->bt_list), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(c->bt_list, "boxed-list");
    GtkWidget *placeholder = gtk_label_new("No Bluetooth devices found");
    gtk_widget_add_css_class(placeholder, "dimmed");
    gtk_widget_set_margin_top(placeholder, 18);
    gtk_widget_set_margin_bottom(placeholder, 18);
    gtk_list_box_set_placeholder(GTK_LIST_BOX(c->bt_list), placeholder);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), c->bt_list);
    gtk_box_append(GTK_BOX(box), group);

    GtkWidget *opts = adw_preferences_group_new();
    c->bt_show_all = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(c->bt_show_all), "Show All Bluetooth Devices");
    g_signal_connect(c->bt_show_all, "notify::active", G_CALLBACK(on_bt_show_all), c);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(opts), c->bt_show_all);
    gtk_box_append(GTK_BOX(box), opts);

    c->bt_next = gtk_button_new_with_label("Next");
    gtk_widget_add_css_class(c->bt_next, "pill");
    gtk_widget_add_css_class(c->bt_next, "suggested-action");
    gtk_widget_set_halign(c->bt_next, GTK_ALIGN_CENTER);
    g_signal_connect(c->bt_next, "clicked", G_CALLBACK(on_bt_next), c);
    gtk_box_append(GTK_BOX(box), c->bt_next);
    return make_page(c, box, "Bluetooth", "bluetooth");
}

/* ---- Device type page ------------------------------------------------------------------- */

static void on_driver_activated(AdwActionRow *row, gpointer user_data) {
    SalusConnectUi *c = user_data;
    c->driver = g_object_get_data(G_OBJECT(row), "salus-driver");
    gchar *title = driver_title(c->driver);
    adw_navigation_page_set_title(c->bt_page, title);
    gchar *text = g_strdup_printf("Turn on your %s and keep it close. It needs to be paired with this "
                                  "computer over Bluetooth.", title);
    gtk_label_set_text(GTK_LABEL(c->bt_intro), text);
    g_free(text);
    g_free(title);
    c->bt_populating = TRUE; /* no refresh from the switch's notify */
    adw_switch_row_set_active(ADW_SWITCH_ROW(c->bt_show_all), FALSE);
    c->bt_populating = FALSE;
    populate_bt_list(c);
    adw_navigation_view_push(ADW_NAVIGATION_VIEW(c->nav), c->bt_page);
}

static AdwNavigationPage *build_type_page(SalusConnectUi *c) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 24);
    gtk_widget_add_css_class(box, "salus-page");
    gtk_box_append(GTK_BOX(box), intro("salus-heart-symbolic", "Connect a Device",
                                       "Choose the band or watch you want to use with Salus. Your "
                                       "measurements, sleep and workouts stay on this computer.",
                                       NULL));
    /* One group per kind of device, in registry order. */
    static const SalusDeviceKind kinds[] = {SALUS_KIND_BAND, SALUS_KIND_WATCH, SALUS_KIND_RING, SALUS_KIND_OTHER};
    for (size_t k = 0; k < G_N_ELEMENTS(kinds); k++) {
        GtkWidget *group = NULL;
        for (const SalusDriver *const *d = salus_drivers(); *d != NULL; d++) {
            if ((*d)->kind != kinds[k]) {
                continue;
            }
            if (group == NULL) {
                group = adw_preferences_group_new();
                adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(group), salus_device_kind_title(kinds[k]));
                gtk_box_append(GTK_BOX(box), group);
            }
            GtkWidget *row = adw_action_row_new();
            gchar *title = driver_title(*d);
            gchar *sub = caps_summary((*d)->caps);
            adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
            adw_action_row_set_subtitle(ADW_ACTION_ROW(row), sub);
            g_free(title);
            g_free(sub);
            GtkWidget *icon = ui_icon((*d)->icon, UI_COLOR_STEPS, 20);
            gtk_widget_add_css_class(icon, "salus-bubble");
            gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
            adw_action_row_add_prefix(ADW_ACTION_ROW(row), icon);
            adw_action_row_add_suffix(ADW_ACTION_ROW(row), gtk_image_new_from_icon_name("go-next-symbolic"));
            gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), TRUE);
            g_object_set_data(G_OBJECT(row), "salus-driver", (gpointer)*d);
            g_signal_connect(row, "activated", G_CALLBACK(on_driver_activated), c);
            adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), row);
        }
    }
    GtkWidget *more = gtk_label_new("More devices can be added as drivers — see docs/ARCHITECTURE.md.");
    gtk_widget_add_css_class(more, "dimmed");
    gtk_widget_add_css_class(more, "caption");
    gtk_label_set_wrap(GTK_LABEL(more), TRUE);
    gtk_box_append(GTK_BOX(box), more);
    return make_page(c, box, "Add Device", "type");
}

/* ---- Public ----------------------------------------------------------------------------- */

GtkWidget *salus_connect_build(SalusApp *app) {
    SalusConnectUi *c = g_new0(SalusConnectUi, 1);
    c->app = app;
    c->cancel_buttons = g_ptr_array_new();
    c->field_rows = g_ptr_array_new();
    app->connect_ui = c;
    c->nav = adw_navigation_view_new();
    adw_navigation_view_add(ADW_NAVIGATION_VIEW(c->nav), build_type_page(c));
    /* Kept alive between pushes: the navigation view drops its own reference on pop. */
    c->bt_page = g_object_ref_sink(build_bt_page(c));
    c->pair_page = g_object_ref_sink(build_pair_page(c));
    return c->nav;
}

void salus_connect_free(SalusApp *app) {
    SalusConnectUi *c = cui(app);
    if (c == NULL) {
        return;
    }
    g_clear_object(&c->bt_page);
    g_clear_object(&c->pair_page);
    g_ptr_array_unref(c->cancel_buttons);
    g_ptr_array_unref(c->field_rows);
    g_free(c->prev_active);
    g_free(c->bt_selected);
    g_free(c->bt_selected_name);
    g_free(c);
    app->connect_ui = NULL;
}

void salus_connect_show(SalusApp *app) {
    SalusConnectUi *c = cui(app);
    if (c == NULL || app->stack == NULL) {
        return;
    }
    g_free(c->prev_active);
    c->prev_active = app->device != NULL ? g_strdup(app->device->config->address) : NULL;
    const gboolean can_cancel = app->saved_devices != NULL && app->saved_devices->len > 0;
    for (guint i = 0; i < c->cancel_buttons->len; i++) {
        gtk_widget_set_visible(g_ptr_array_index(c->cancel_buttons, i), can_cancel);
    }
    adw_navigation_view_pop_to_tag(ADW_NAVIGATION_VIEW(c->nav), "type");
    gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "setup");
}

void salus_connect_cancel(SalusApp *app) {
    SalusConnectUi *c = cui(app);
    /* A device being tried out but never connected is dropped: go back to the one before. */
    if (app->device != NULL && salus_app_saved_device(app, app->device->config->address) == NULL) {
        if (c->prev_active == NULL || !salus_app_activate_device(app, c->prev_active)) {
            salus_app_set_device(app, NULL);
        }
    }
    if (app->saved_devices != NULL && app->saved_devices->len > 0) {
        gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "main");
        salus_dashboard_refresh(app);
    }
}

void salus_connect_set_busy(SalusApp *app, gboolean busy) {
    SalusConnectUi *c = cui(app);
    if (c == NULL) {
        return;
    }
    c->busy = busy;
    gtk_widget_set_visible(c->pair_spinner, busy);
    gtk_widget_set_sensitive(c->bt_list, !busy);
    gtk_widget_set_sensitive(c->bt_refresh, !busy);
    for (guint i = 0; i < c->field_rows->len; i++) {
        gtk_widget_set_sensitive(g_ptr_array_index(c->field_rows, i), !busy);
    }
    if (c->driver != NULL) {
        pair_update_sensitivity(c);
        bt_update(c);
    }
}

void salus_connect_set_status(SalusApp *app, const char *text) {
    SalusConnectUi *c = cui(app);
    if (c != NULL) {
        gtk_label_set_text(GTK_LABEL(c->pair_status), text != NULL ? text : "");
    }
}
