#include "ui_private.h"

#include "salus/profile.h"
#include "salus/spp/fitness_sync.h"
#include "salus/spp/wire.h"
#include "salus/weight_log.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define GOAL_STEPS 10000
#define GOAL_CALORIES 400
#define GOAL_VITALITY 100

/* Readings further apart than this are drawn as separate line segments (off-wrist, gaps). */
#define TREND_MAX_GAP_MIN 60
#define TREND_STEP_BUCKET_MIN 30
#define TREND_BUCKETS (UI_DAY_MINUTES / TREND_STEP_BUCKET_MIN)

static void dashboard_update_date_navs(AppState *state);

/* ======================================================================================== */
/* Lifecycle                                                                                */
/* ======================================================================================== */

SalusDashboardUi *salus_dashboard_ui_new(void) {
    SalusDashboardUi *dash = g_new0(SalusDashboardUi, 1);
    dash->battery = -1;
    dash->trend_hover_x = -1.0;
    dash->sleep_hover_x = -1.0;
    dash->date_titles = g_ptr_array_new();
    dash->date_next = g_ptr_array_new();
    dash->date_today = g_ptr_array_new();
    dash->calendars = g_ptr_array_new();
    dash->night = g_new0(UiNight, 1);
    return dash;
}

void salus_dashboard_ui_free(SalusDashboardUi *dash) {
    if (dash == NULL) {
        return;
    }
    g_ptr_array_free(dash->date_titles, TRUE);
    g_ptr_array_free(dash->date_next, TRUE);
    g_ptr_array_free(dash->date_today, TRUE);
    g_ptr_array_free(dash->calendars, TRUE);
    g_clear_object(&dash->metric_page);
    g_free(dash->night);
    g_free(dash->status_text);
    g_free(dash);
}

/* ======================================================================================== */
/* View date                                                                                */
/* ======================================================================================== */

static GDate *today_date(void) {
    GDateTime *now = g_date_time_new_now_local();
    GDate *d = g_date_new_dmy((GDateDay)g_date_time_get_day_of_month(now),
                              (GDateMonth)g_date_time_get_month(now),
                              (GDateYear)g_date_time_get_year(now));
    g_date_time_unref(now);
    return d;
}

GDate *ui_view_date(AppState *state) {
    if (state->health_view_date == NULL) {
        state->health_view_date = today_date();
    }
    return state->health_view_date;
}

gchar *ui_day_key(GDate *d) {
    return g_strdup_printf("%04u-%02u-%02u", (guint)g_date_get_year(d), (guint)g_date_get_month(d),
                           (guint)g_date_get_day(d));
}

gboolean ui_viewing_today(AppState *state) {
    GDate *today = today_date();
    const gboolean same = g_date_compare(today, ui_view_date(state)) == 0;
    g_date_free(today);
    return same;
}

void ui_set_view_date(AppState *state, GDate *d) {
    GDate *today = today_date();
    GDate *vd = ui_view_date(state);
    *vd = g_date_compare(d, today) > 0 ? *today : *d; /* no future days */
    g_date_free(today);
    salus_dashboard_refresh(state);
}

static void on_date_prev(GtkButton *b, gpointer user_data) {
    (void)b;
    AppState *state = user_data;
    GDate d = *ui_view_date(state);
    g_date_subtract_days(&d, 1);
    ui_set_view_date(state, &d);
}

static void on_date_next(GtkButton *b, gpointer user_data) {
    (void)b;
    AppState *state = user_data;
    GDate d = *ui_view_date(state);
    g_date_add_days(&d, 1);
    ui_set_view_date(state, &d);
}

static void on_date_today(GtkButton *b, gpointer user_data) {
    (void)b;
    AppState *state = user_data;
    GDate *t = today_date();
    ui_set_view_date(state, t);
    g_date_free(t);
}

static void on_calendar_day_selected(GtkCalendar *cal, gpointer user_data) {
    AppState *state = user_data;
    if (g_object_get_data(G_OBJECT(cal), "salus-syncing") != NULL) {
        return;
    }
    GDateTime *dt = gtk_calendar_get_date(cal);
    GDate *d = g_date_new_dmy((GDateDay)g_date_time_get_day_of_month(dt), (GDateMonth)g_date_time_get_month(dt),
                              (GDateYear)g_date_time_get_year(dt));
    g_date_time_unref(dt);
    GtkWidget *popover = gtk_widget_get_ancestor(GTK_WIDGET(cal), GTK_TYPE_POPOVER);
    if (popover != NULL) {
        gtk_popover_popdown(GTK_POPOVER(popover));
    }
    ui_set_view_date(state, d);
    g_date_free(d);
}

GtkWidget *ui_date_nav_new(AppState *state, SalusDashboardUi *dash) {
    GtkWidget *bar = gtk_center_box_new();

    GtkWidget *prev = gtk_button_new_from_icon_name("go-previous-symbolic");
    gtk_widget_add_css_class(prev, "circular");
    gtk_widget_add_css_class(prev, "flat");
    gtk_widget_set_tooltip_text(prev, "Previous day");
    g_signal_connect(prev, "clicked", G_CALLBACK(on_date_prev), state);

    GtkWidget *title = ui_label("Today", "salus-date-title", 0.5f);
    GtkWidget *title_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(title_box), title);
    gtk_box_append(GTK_BOX(title_box), gtk_image_new_from_icon_name("pan-down-symbolic"));
    GtkWidget *menu = gtk_menu_button_new();
    gtk_menu_button_set_child(GTK_MENU_BUTTON(menu), title_box);
    gtk_widget_add_css_class(menu, "flat");
    gtk_widget_set_tooltip_text(menu, "Pick a day");
    GtkWidget *cal = gtk_calendar_new();
    g_signal_connect(cal, "day-selected", G_CALLBACK(on_calendar_day_selected), state);
    GtkWidget *popover = gtk_popover_new();
    gtk_popover_set_child(GTK_POPOVER(popover), cal);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(menu), popover);

    GtkWidget *end = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *today = gtk_button_new_with_label("Today");
    gtk_widget_add_css_class(today, "flat");
    gtk_widget_set_valign(today, GTK_ALIGN_CENTER);
    g_signal_connect(today, "clicked", G_CALLBACK(on_date_today), state);
    GtkWidget *next = gtk_button_new_from_icon_name("go-next-symbolic");
    gtk_widget_add_css_class(next, "circular");
    gtk_widget_add_css_class(next, "flat");
    gtk_widget_set_tooltip_text(next, "Next day");
    g_signal_connect(next, "clicked", G_CALLBACK(on_date_next), state);
    gtk_box_append(GTK_BOX(end), today);
    gtk_box_append(GTK_BOX(end), next);

    gtk_center_box_set_start_widget(GTK_CENTER_BOX(bar), prev);
    gtk_center_box_set_center_widget(GTK_CENTER_BOX(bar), menu);
    gtk_center_box_set_end_widget(GTK_CENTER_BOX(bar), end);

    g_ptr_array_add(dash->date_titles, title);
    g_ptr_array_add(dash->date_next, next);
    g_ptr_array_add(dash->date_today, today);
    g_ptr_array_add(dash->calendars, cal);
    return bar;
}

static void dashboard_update_date_navs(AppState *state) {
    SalusDashboardUi *dash = state->dash_ui;
    GDate *vd = ui_view_date(state);
    GDate *today = today_date();
    const int delta = g_date_days_between(vd, today);
    gchar *title = NULL;
    GDateTime *sel = g_date_time_new_local((gint)g_date_get_year(vd), (gint)g_date_get_month(vd),
                                           (gint)g_date_get_day(vd), 12, 0, 0);
    if (delta == 0) {
        title = g_strdup("Today");
    } else if (delta == 1) {
        title = g_strdup("Yesterday");
    } else {
        title = g_date_time_format(sel, g_date_get_year(vd) == g_date_get_year(today) ? "%a %-d %b" : "%a %-d %b %Y");
    }
    for (guint i = 0; i < dash->date_titles->len; i++) {
        gtk_label_set_text(GTK_LABEL(g_ptr_array_index(dash->date_titles, i)), title);
        gtk_widget_set_sensitive(GTK_WIDGET(g_ptr_array_index(dash->date_next, i)), delta > 0);
        gtk_widget_set_visible(GTK_WIDGET(g_ptr_array_index(dash->date_today, i)), delta > 0);
        GObject *cal = g_ptr_array_index(dash->calendars, i);
        g_object_set_data(cal, "salus-syncing", GINT_TO_POINTER(1));
        gtk_calendar_set_date(GTK_CALENDAR(cal), sel);
        g_object_set_data(cal, "salus-syncing", NULL);
    }
    g_date_time_unref(sel);
    g_free(title);
    g_date_free(today);
}

/* ======================================================================================== */
/* Shell: connection state, toasts, dialogs                                                 */
/* ======================================================================================== */

gboolean ui_is_connected(AppState *state) {
    return state->sock_fd >= 0 && (state->dash_ui == NULL || !state->dash_ui->busy);
}

void ui_toast(AppState *state, const char *text) {
    SalusDashboardUi *dash = state->dash_ui;
    if (dash == NULL || dash->toast_overlay == NULL || text == NULL) {
        return;
    }
    AdwToast *toast = adw_toast_new(text);
    adw_toast_set_timeout(toast, 3);
    adw_toast_overlay_add_toast(ADW_TOAST_OVERLAY(dash->toast_overlay), toast);
}

void ui_connect_saved(AppState *state) {
    if (!salus_ui_has_saved_device(state)) {
        salus_ui_show_setup(state);
        return;
    }
    gchar *addr = g_strdup(state->selected_address);
    start_connect_for_address(state, addr);
    g_free(addr);
}

void ui_show_page(AppState *state, const char *name) {
    SalusDashboardUi *dash = state->dash_ui;
    if (dash != NULL && dash->view_stack != NULL) {
        adw_view_stack_set_visible_child_name(ADW_VIEW_STACK(dash->view_stack), name);
    }
}

static gboolean status_is_error(const char *s) {
    return s != NULL && (strstr(s, "fail") != NULL || strstr(s, "Fail") != NULL || strstr(s, "Invalid") != NULL ||
                         strstr(s, "error") != NULL);
}

void salus_dashboard_update_connection(AppState *state) {
    SalusDashboardUi *dash = state->dash_ui;
    if (dash == NULL || dash->banner == NULL) {
        return;
    }
    const gboolean connected = ui_is_connected(state);
    const char *icon = connected ? "bluetooth-active-symbolic"
                       : dash->busy ? "bluetooth-acquiring-symbolic"
                                    : "bluetooth-disconnected-symbolic";
    gtk_image_set_from_icon_name(GTK_IMAGE(dash->conn_icon), icon);

    const char *name = state->device_display_name != NULL ? state->device_display_name : "the band";
    gchar *tip = connected   ? g_strdup_printf("Connected to %s", name)
                 : dash->busy ? g_strdup(dash->status_text != NULL ? dash->status_text : "Connecting…")
                              : g_strdup_printf("Not connected — click to connect to %s", name);
    gtk_widget_set_tooltip_text(dash->conn_button, tip);
    g_free(tip);
    gtk_widget_set_sensitive(dash->conn_button, !dash->busy);
    gtk_widget_set_sensitive(dash->sync_button, connected);

    if (connected) {
        adw_banner_set_revealed(ADW_BANNER(dash->banner), FALSE);
    } else if (dash->busy) {
        adw_banner_set_title(ADW_BANNER(dash->banner), dash->status_text != NULL ? dash->status_text : "Connecting…");
        adw_banner_set_button_label(ADW_BANNER(dash->banner), NULL);
        adw_banner_set_revealed(ADW_BANNER(dash->banner), TRUE);
    } else {
        gchar *title = status_is_error(dash->status_text)
                           ? g_strdup_printf("%s Showing saved data.", dash->status_text)
                           : g_strdup("Not connected — showing saved data");
        adw_banner_set_title(ADW_BANNER(dash->banner), title);
        g_free(title);
        adw_banner_set_button_label(ADW_BANNER(dash->banner),
                                    salus_ui_has_saved_device(state) ? "Connect" : "Set Up");
        adw_banner_set_revealed(ADW_BANNER(dash->banner), TRUE);
    }
    ui_device_refresh(state);
}

void salus_dashboard_set_busy(AppState *state, gboolean busy) {
    if (state->dash_ui != NULL) {
        state->dash_ui->busy = busy;
        salus_dashboard_update_connection(state);
    }
}

void salus_dashboard_set_status(AppState *state, const char *text) {
    SalusDashboardUi *dash = state->dash_ui;
    if (dash == NULL) {
        return;
    }
    g_free(dash->status_text);
    dash->status_text = g_strdup(text);
    salus_dashboard_update_connection(state);
}

void salus_dashboard_toast(AppState *state, const char *text) {
    ui_toast(state, text);
}

void salus_dashboard_set_toast_overlay(SalusDashboardUi *dash, GtkWidget *overlay) {
    dash->toast_overlay = overlay;
}

static void on_banner_button(AdwBanner *banner, gpointer user_data) {
    (void)banner;
    ui_connect_saved(user_data);
}

static void on_conn_button(GtkButton *b, gpointer user_data) {
    (void)b;
    AppState *state = user_data;
    if (ui_is_connected(state)) {
        ui_show_page(state, "device");
    } else {
        ui_connect_saved(state);
    }
}

static void on_sync_button(GtkButton *b, gpointer user_data) {
    (void)b;
    AppState *state = user_data;
    if (ui_is_connected(state)) {
        salus_fitness_sync_resync(state);
        ui_toast(state, "Syncing health data…");
    }
}

/* ---- Log dialog --------------------------------------------------------------------------- */

static void on_log_copy(GtkButton *b, gpointer user_data) {
    AppState *state = user_data;
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(state->log_buffer, &s, &e);
    gchar *text = gtk_text_buffer_get_text(state->log_buffer, &s, &e, FALSE);
    gdk_clipboard_set_text(gtk_widget_get_clipboard(GTK_WIDGET(b)), text);
    g_free(text);
    ui_toast(state, "Log copied");
}

static gboolean scroll_log_to_end(gpointer data) {
    GtkTextView *tv = data;
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(gtk_text_view_get_buffer(tv), &end);
    gtk_text_view_scroll_to_iter(tv, &end, 0.0, FALSE, 0.0, 1.0);
    g_object_unref(tv);
    return G_SOURCE_REMOVE;
}

void ui_show_log_dialog(AppState *state) {
    if (state->log_buffer == NULL || state->window == NULL) {
        return;
    }
    AdwDialog *dialog = adw_dialog_new();
    adw_dialog_set_title(dialog, "Connection Log");
    adw_dialog_set_content_width(dialog, 760);
    adw_dialog_set_content_height(dialog, 560);

    GtkWidget *tv = gtk_text_view_new_with_buffer(state->log_buffer);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(tv), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(tv), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(tv), 12);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(tv), 12);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(tv), 8);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(tv), 8);
    gtk_widget_add_css_class(tv, "salus-log");
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), tv);
    gtk_widget_set_vexpand(scroll, TRUE);

    GtkWidget *header = adw_header_bar_new();
    GtkWidget *copy = gtk_button_new_from_icon_name("edit-copy-symbolic");
    gtk_widget_set_tooltip_text(copy, "Copy log");
    g_signal_connect(copy, "clicked", G_CALLBACK(on_log_copy), state);
    adw_header_bar_pack_start(ADW_HEADER_BAR(header), copy);

    GtkWidget *tb = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(tb), header);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(tb), scroll);
    adw_dialog_set_child(dialog, tb);
    adw_dialog_present(dialog, state->window);
    g_idle_add(scroll_log_to_end, g_object_ref(tv));
}

/* ---- Weight dialog ------------------------------------------------------------------------ */

static void on_weight_response(AdwAlertDialog *dialog, const char *response, gpointer user_data) {
    AppState *state = user_data;
    if (g_strcmp0(response, "save") != 0) {
        return;
    }
    GtkWidget *spin = g_object_get_data(G_OBJECT(dialog), "salus-spin");
    const double kg = gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin));
    if (salus_weight_log_add_today(state, (float)kg)) {
        ui_toast(state, "Weight saved");
        salus_dashboard_refresh(state);
    }
}

void ui_show_weight_dialog(AppState *state) {
    AdwDialog *dialog = adw_alert_dialog_new("Log Weight", "Today's weight in kilograms.");
    GtkAdjustment *adj = gtk_adjustment_new(70.0, 30.0, 300.0, 0.1, 1.0, 0.0);
    guint last_date = 0;
    const float last_kg = salus_weight_log_latest_kg(state, &last_date);
    if (last_kg > 0.0f) {
        gtk_adjustment_set_value(adj, last_kg);
    }
    GtkWidget *spin = gtk_spin_button_new(adj, 0.1, 1);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(spin), TRUE);
    gtk_widget_set_halign(spin, GTK_ALIGN_CENTER);
    adw_alert_dialog_set_extra_child(ADW_ALERT_DIALOG(dialog), spin);
    g_object_set_data(G_OBJECT(dialog), "salus-spin", spin);
    adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(dialog), "cancel", "_Cancel", "save", "_Save", NULL);
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog), "save", ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "save");
    g_signal_connect(dialog, "response", G_CALLBACK(on_weight_response), state);
    adw_dialog_present(dialog, state->window);
}

/* ---- Menu actions ------------------------------------------------------------------------- */

static void act_sync(GSimpleAction *a, GVariant *p, gpointer user_data) {
    (void)a;
    (void)p;
    on_sync_button(NULL, user_data);
}

static void act_log(GSimpleAction *a, GVariant *p, gpointer user_data) {
    (void)a;
    (void)p;
    ui_show_log_dialog(user_data);
}

static void act_change_device(GSimpleAction *a, GVariant *p, gpointer user_data) {
    (void)a;
    (void)p;
    salus_ui_show_setup(user_data);
}

static void act_about(GSimpleAction *a, GVariant *p, gpointer user_data) {
    (void)a;
    (void)p;
    AppState *state = user_data;
    AdwDialog *about = adw_about_dialog_new();
    adw_about_dialog_set_application_name(ADW_ABOUT_DIALOG(about), "Salus");
    adw_about_dialog_set_application_icon(ADW_ABOUT_DIALOG(about), "salus-watch-symbolic");
    adw_about_dialog_set_comments(ADW_ABOUT_DIALOG(about),
                                  "Health data from your smart band or watch on Linux: sync measurements, "
                                  "sleep and workouts, forward notifications and browse your history offline.");
    adw_about_dialog_set_license_type(ADW_ABOUT_DIALOG(about), GTK_LICENSE_GPL_3_0);
    adw_about_dialog_set_website(ADW_ABOUT_DIALOG(about), "https://github.com/notfatnotfunny/miwear-band");
    adw_dialog_present(about, state->window);
}

/* ======================================================================================== */
/* Shell                                                                                    */
/* ======================================================================================== */

GtkWidget *salus_dashboard_build(AppState *state, SalusDashboardUi *dash) {
    state->dash_ui = dash;
    dash->state = state;

    dash->view_stack = adw_view_stack_new();
    adw_view_stack_add_titled_with_icon(ADW_VIEW_STACK(dash->view_stack), ui_today_page_new(state, dash), "today",
                                        "Health", "salus-heart-symbolic");
    adw_view_stack_add_titled_with_icon(ADW_VIEW_STACK(dash->view_stack), mi_sleep_page_new(state, dash), "sleep",
                                        "Sleep", "salus-moon-symbolic");
    adw_view_stack_add_titled_with_icon(ADW_VIEW_STACK(dash->view_stack), ui_workouts_page_new(state, dash),
                                        "workouts", "Workouts", "salus-run-symbolic");
    adw_view_stack_add_titled_with_icon(ADW_VIEW_STACK(dash->view_stack), ui_device_page_new(state, dash), "device",
                                        "Device", "salus-watch-symbolic");

    dash->header = adw_header_bar_new();
    GtkWidget *switcher = adw_view_switcher_new();
    adw_view_switcher_set_stack(ADW_VIEW_SWITCHER(switcher), ADW_VIEW_STACK(dash->view_stack));
    adw_view_switcher_set_policy(ADW_VIEW_SWITCHER(switcher), ADW_VIEW_SWITCHER_POLICY_WIDE);
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(dash->header), switcher);

    dash->conn_button = gtk_button_new();
    dash->conn_icon = gtk_image_new_from_icon_name("bluetooth-disconnected-symbolic");
    gtk_button_set_child(GTK_BUTTON(dash->conn_button), dash->conn_icon);
    g_signal_connect(dash->conn_button, "clicked", G_CALLBACK(on_conn_button), state);
    adw_header_bar_pack_start(ADW_HEADER_BAR(dash->header), dash->conn_button);

    GMenu *menu = g_menu_new();
    GMenu *sec1 = g_menu_new();
    g_menu_append(sec1, "Sync Now", "dash.sync");
    g_menu_append(sec1, "Change Device…", "dash.change-device");
    g_menu_append(sec1, "Connection Log", "dash.log");
    GMenu *sec2 = g_menu_new();
    g_menu_append(sec2, "About Salus", "dash.about");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(sec1));
    g_menu_append_section(menu, NULL, G_MENU_MODEL(sec2));
    GtkWidget *menu_btn = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu_btn), "open-menu-symbolic");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu_btn), G_MENU_MODEL(menu));
    gtk_menu_button_set_primary(GTK_MENU_BUTTON(menu_btn), TRUE);
    gtk_widget_set_tooltip_text(menu_btn, "Main menu");
    g_object_unref(sec1);
    g_object_unref(sec2);
    g_object_unref(menu);
    adw_header_bar_pack_end(ADW_HEADER_BAR(dash->header), menu_btn);

    dash->sync_button = gtk_button_new_from_icon_name("view-refresh-symbolic");
    gtk_widget_set_tooltip_text(dash->sync_button, "Sync health data");
    g_signal_connect(dash->sync_button, "clicked", G_CALLBACK(on_sync_button), state);
    adw_header_bar_pack_end(ADW_HEADER_BAR(dash->header), dash->sync_button);

    dash->banner = adw_banner_new("Not connected — showing saved data");
    adw_banner_set_button_label(ADW_BANNER(dash->banner), "Connect");
    g_signal_connect(dash->banner, "button-clicked", G_CALLBACK(on_banner_button), state);

    dash->switcher_bar = adw_view_switcher_bar_new();
    adw_view_switcher_bar_set_stack(ADW_VIEW_SWITCHER_BAR(dash->switcher_bar), ADW_VIEW_STACK(dash->view_stack));

    GtkWidget *tb = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(tb), dash->header);
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(tb), dash->banner);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(tb), dash->view_stack);
    adw_toolbar_view_add_bottom_bar(ADW_TOOLBAR_VIEW(tb), dash->switcher_bar);

    static const GActionEntry entries[] = {
        {.name = "sync", .activate = act_sync},
        {.name = "log", .activate = act_log},
        {.name = "change-device", .activate = act_change_device},
        {.name = "about", .activate = act_about},
    };
    GSimpleActionGroup *group = g_simple_action_group_new();
    g_action_map_add_action_entries(G_ACTION_MAP(group), entries, G_N_ELEMENTS(entries), state);
    gtk_widget_insert_action_group(tb, "dash", G_ACTION_GROUP(group));
    g_object_unref(group);

    AdwNavigationPage *main_page = adw_navigation_page_new_with_tag(tb, "Salus", "main");
    dash->nav_view = adw_navigation_view_new();
    adw_navigation_view_add(ADW_NAVIGATION_VIEW(dash->nav_view), main_page);
    return dash->nav_view;
}

void salus_dashboard_add_breakpoints(GtkWidget *window, SalusDashboardUi *dash) {
    AdwBreakpoint *bp = adw_breakpoint_new(adw_breakpoint_condition_parse("max-width: 640sp"));
    GValue v_true = G_VALUE_INIT;
    g_value_init(&v_true, G_TYPE_BOOLEAN);
    g_value_set_boolean(&v_true, TRUE);
    adw_breakpoint_add_setter(bp, G_OBJECT(dash->switcher_bar), "reveal", &v_true);
    GValue v_null = G_VALUE_INIT;
    g_value_init(&v_null, GTK_TYPE_WIDGET);
    g_value_set_object(&v_null, NULL);
    adw_breakpoint_add_setter(bp, G_OBJECT(dash->header), "title-widget", &v_null);
    g_value_unset(&v_true);
    g_value_unset(&v_null);
    adw_application_window_add_breakpoint(ADW_APPLICATION_WINDOW(window), bp);
}

/* ======================================================================================== */
/* Health (Today) page                                                                      */
/* ======================================================================================== */

typedef struct {
    const char *name;
    const char *label;
    const char *empty;
    UiSeries series;
    UiColor color;
} TrendSpec;

static const TrendSpec trend_specs[UI_SERIES_COUNT] = {
    {"hr", "Heart Rate", "No heart-rate readings for this day", UI_SERIES_HR, UI_COLOR_HR},
    {"spo2", "SpO₂", "No SpO₂ readings for this day", UI_SERIES_SPO2, UI_COLOR_SPO2},
    {"stress", "Stress", "No stress readings for this day", UI_SERIES_STRESS, UI_COLOR_STRESS},
    {"steps", "Steps", "No steps recorded for this day", UI_SERIES_STEPS, UI_COLOR_STEPS},
};

/* ---- Activity rings ------------------------------------------------------------------- */

static void draw_rings_cb(GtkDrawingArea *area, cairo_t *cr, int w, int h, gpointer user_data) {
    (void)area;
    SalusDashboardUi *dash = user_data;
    static const UiColor colors[3] = {UI_COLOR_STEPS, UI_COLOR_CALORIES, UI_COLOR_ACTIVE};
    const double size = MIN(w, h);
    ui_draw_rings(cr, w / 2.0, h / 2.0, size / 2.0 - 2.0, size * 0.11, dash->ring_ratio, colors, 3);
}

static GtkWidget *ring_legend_row(UiColor color, const char *title, GtkWidget **value_out) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *dot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(dot, "salus-dot");
    gtk_widget_add_css_class(dot, ui_color_css_class(color));
    gtk_widget_set_valign(dot, GTK_ALIGN_CENTER);
    GtkWidget *texts = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(texts), ui_label(title, "caption dimmed salus-ring-label", 0.0f));
    GtkWidget *value = ui_label("—", "salus-ring-value numeric", 0.0f);
    gtk_label_set_use_markup(GTK_LABEL(value), TRUE);
    gtk_box_append(GTK_BOX(texts), value);
    gtk_box_append(GTK_BOX(row), dot);
    gtk_box_append(GTK_BOX(row), texts);
    *value_out = value;
    return row;
}

static void on_tile_clicked(GtkButton *b, gpointer user_data);

static GtkWidget *build_activity_card(AppState *state, SalusDashboardUi *dash) {
    GtkWidget *inner = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    dash->ring_badge = ui_label("LIVE", "salus-live", 0.5f);
    gtk_widget_set_visible(dash->ring_badge, FALSE);
    GtkWidget *end = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(end), dash->ring_badge);
    GtkWidget *chev = gtk_image_new_from_icon_name("go-next-symbolic");
    gtk_widget_add_css_class(chev, "dimmed");
    gtk_box_append(GTK_BOX(end), chev);
    gtk_box_append(GTK_BOX(inner), ui_card_header("Activity", NULL, end));

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 22);
    dash->rings = gtk_drawing_area_new();
    gtk_widget_set_size_request(dash->rings, 136, 136);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(dash->rings), draw_rings_cb, dash, NULL);
    GtkWidget *legend = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_valign(legend, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(legend, TRUE);
    gtk_box_append(GTK_BOX(legend), ring_legend_row(UI_COLOR_STEPS, "Steps", &dash->ring_steps));
    gtk_box_append(GTK_BOX(legend), ring_legend_row(UI_COLOR_CALORIES, "Active calories", &dash->ring_calories));
    gtk_box_append(GTK_BOX(legend), ring_legend_row(UI_COLOR_ACTIVE, "Active minutes", &dash->ring_active));
    gtk_box_append(GTK_BOX(row), dash->rings);
    gtk_box_append(GTK_BOX(row), legend);
    gtk_box_append(GTK_BOX(inner), row);

    /* The whole card opens the steps chart. */
    GtkWidget *card = gtk_button_new();
    gtk_button_set_child(GTK_BUTTON(card), inner);
    gtk_widget_add_css_class(card, "card");
    gtk_widget_add_css_class(card, "salus-card");
    gtk_widget_set_tooltip_text(card, "Steps through the day");
    g_object_set_data(G_OBJECT(card), "salus-tile", (gpointer) "steps");
    g_signal_connect(card, "clicked", G_CALLBACK(on_tile_clicked), state);
    return card;
}

static void set_ring_value(GtkWidget *label, long value, long goal, const char *unit) {
    gchar v[32], g[32];
    if (value < 0) {
        gtk_label_set_markup(GTK_LABEL(label), "—");
        return;
    }
    ui_format_thousands(value, v, sizeof(v));
    ui_format_thousands(goal, g, sizeof(g));
    gchar *m = g_markup_printf_escaped("%s<span weight=\"normal\" alpha=\"55%%\" size=\"small\"> / %s%s</span>", v, g,
                                       unit);
    gtk_label_set_markup(GTK_LABEL(label), m);
    g_free(m);
}

/* ---- Tiles ---------------------------------------------------------------------------- */

static void tile_build(UiTile *t, AppState *state, const char *id, const char *title, const char *icon,
                       UiColor color, gboolean clickable, int spark_series) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *bubble = ui_icon(icon, color, 16);
    gtk_widget_add_css_class(bubble, "salus-bubble");
    gtk_box_append(GTK_BOX(head), bubble);
    GtkWidget *tl = ui_label(title, "salus-tile-title dimmed", 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(tl), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(tl, TRUE);
    gtk_box_append(GTK_BOX(head), tl);
    if (clickable) {
        GtkWidget *chev = gtk_image_new_from_icon_name("go-next-symbolic");
        gtk_widget_add_css_class(chev, "dimmed");
        gtk_box_append(GTK_BOX(head), chev);
    }
    gtk_box_append(GTK_BOX(box), head);

    GtkWidget *vrow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_margin_top(vrow, 8);
    t->value = ui_label("—", "salus-tile-value numeric", 0.0f);
    t->unit = ui_label("", "salus-tile-unit", 0.0f);
    gtk_widget_set_valign(t->unit, GTK_ALIGN_BASELINE_FILL);
    gtk_widget_set_valign(t->value, GTK_ALIGN_BASELINE_FILL);
    gtk_box_append(GTK_BOX(vrow), t->value);
    gtk_box_append(GTK_BOX(vrow), t->unit);
    gtk_box_append(GTK_BOX(box), vrow);

    t->sub = ui_label("", "caption dimmed", 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(t->sub), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(box), t->sub);
    if (spark_series >= 0) {
        t->spark = ui_sparkline_new(state, (UiSeries)spark_series);
        gtk_widget_set_margin_top(t->spark, 6);
        gtk_box_append(GTK_BOX(box), t->spark);
    }

    if (clickable) {
        t->root = gtk_button_new();
        gtk_button_set_child(GTK_BUTTON(t->root), box);
        g_object_set_data(G_OBJECT(t->root), "salus-tile", (gpointer)id);
        g_signal_connect(t->root, "clicked", G_CALLBACK(on_tile_clicked), state);
    } else {
        t->root = box;
    }
    gtk_widget_add_css_class(t->root, "card");
    gtk_widget_add_css_class(t->root, "salus-tile");
    t->icon = bubble;
}

static void tile_set(UiTile *t, const char *value, const char *unit, const char *sub) {
    gtk_label_set_text(GTK_LABEL(t->value), value != NULL ? value : "—");
    gtk_label_set_text(GTK_LABEL(t->unit), unit != NULL ? unit : "");
    gtk_widget_set_visible(t->unit, unit != NULL && unit[0] != '\0');
    gtk_label_set_text(GTK_LABEL(t->sub), sub != NULL ? sub : "");
}

/* ---- Day chart ------------------------------------------------------------------------ */

static void trend_step_buckets(const int *values, int *buckets, int *max_out) {
    int max = 0;
    for (int b = 0; b < TREND_BUCKETS; b++) {
        int sum = 0;
        for (int m = b * TREND_STEP_BUCKET_MIN; m < (b + 1) * TREND_STEP_BUCKET_MIN; m++) {
            if (values[m] > 0) {
                sum += values[m];
            }
        }
        buckets[b] = sum;
        max = MAX(max, sum);
    }
    *max_out = max;
}

static int nice_ceil(int v) {
    static const int steps[] = {100, 200, 400, 500, 800, 1000, 2000, 4000, 5000};
    for (size_t i = 0; i < G_N_ELEMENTS(steps); i++) {
        if (v <= steps[i]) {
            return steps[i];
        }
    }
    return ((v + 999) / 1000) * 1000;
}

static void trend_range(UiSeries s, const int *values, int bucket_max, int *lo, int *hi) {
    int vmin = G_MAXINT, vmax = G_MININT;
    for (int m = 0; m < UI_DAY_MINUTES; m++) {
        if (values[m] >= 0) {
            vmin = MIN(vmin, values[m]);
            vmax = MAX(vmax, values[m]);
        }
    }
    switch (s) {
        case UI_SERIES_HR:
            *lo = MAX(0, ((vmin - 5) / 10) * 10);
            *hi = ((vmax + 14) / 10) * 10;
            if (*hi - *lo < 40) {
                *hi = *lo + 40;
            }
            break;
        case UI_SERIES_SPO2:
            *lo = MIN(90, ((vmin - 1) / 5) * 5);
            *hi = 100;
            break;
        case UI_SERIES_STRESS:
            *lo = 0;
            *hi = 100;
            break;
        default:
            *lo = 0;
            *hi = nice_ceil(MAX(bucket_max, 1));
            break;
    }
}

static void trend_format(UiSeries s, int v, gchar *buf, size_t len) {
    switch (s) {
        case UI_SERIES_HR: g_snprintf(buf, len, "%d bpm", v); break;
        case UI_SERIES_SPO2: g_snprintf(buf, len, "%d%%", v); break;
        case UI_SERIES_STRESS: g_snprintf(buf, len, "%d · %s", v, salus_pressure_level_label(v)); break;
        default: {
            gchar t[32];
            ui_format_thousands(v, t, sizeof(t));
            g_snprintf(buf, len, "%s steps", t);
            break;
        }
    }
}

static void draw_trend(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
    AppState *state = user_data;
    SalusDashboardUi *dash = state->dash_ui;
    GtkWidget *w = GTK_WIDGET(area);
    if (dash == NULL || width < 60 || height < 60) {
        return;
    }
    const TrendSpec *spec = &trend_specs[dash->trend_metric];
    const UiSeries series = spec->series;
    double col[3];
    ui_color_rgb(spec->color, col);

    const double left = 34.0, right = 6.0, top = 8.0, bottom = 22.0;
    const double pw = width - left - right, ph = height - top - bottom;

    int *values = g_new(int, UI_DAY_MINUTES);
    gchar *key = ui_day_key(ui_view_date(state));
    const guint n = ui_series_collect(state, key, series, values);
    g_free(key);

    cairo_set_line_width(cr, 1.0);
    for (int hh = 0; hh <= 24; hh += 6) {
        const double x = left + pw * hh / 24.0;
        gchar t[8];
        g_snprintf(t, sizeof(t), "%02d:00", hh % 24);
        ui_draw_text(cr, w, t, CLAMP(x, left + 14, left + pw - 14), top + ph + 16, 10.5, 0.55, 0.5);
    }
    if (n == 0) {
        ui_cairo_set_fg(cr, w, 0.18);
        cairo_move_to(cr, left, round(top + ph) + 0.5);
        cairo_line_to(cr, left + pw, round(top + ph) + 0.5);
        cairo_stroke(cr);
        ui_draw_text(cr, w, spec->empty, left + pw / 2, top + ph / 2, 12.5, 0.55, 0.5);
        g_free(values);
        return;
    }

    int buckets[TREND_BUCKETS];
    int bucket_max = 0;
    if (series == UI_SERIES_STEPS) {
        trend_step_buckets(values, buckets, &bucket_max);
    }
    int lo, hi;
    trend_range(series, values, bucket_max, &lo, &hi);
    const int range = MAX(hi - lo, 1);
    const int divs = (range % 4 == 0) ? 4 : (range % 5 == 0) ? 5 : (range % 2 == 0) ? 2 : 1;
#define TX(m) (left + pw * (double)(m) / (double)UI_DAY_MINUTES)
#define TY(v) (top + ph * (1.0 - ((double)(v) - lo) / (double)range))

    for (int k = 0; k <= divs; k++) {
        const int v = lo + range * k / divs;
        ui_cairo_set_fg(cr, w, k == 0 ? 0.18 : 0.08);
        cairo_move_to(cr, left, round(TY(v)) + 0.5);
        cairo_line_to(cr, left + pw, round(TY(v)) + 0.5);
        cairo_stroke(cr);
        gchar t[16];
        if (series == UI_SERIES_STEPS && v >= 1000) {
            g_snprintf(t, sizeof(t), "%.*fk", v % 1000 ? 1 : 0, v / 1000.0);
        } else {
            g_snprintf(t, sizeof(t), "%d", v);
        }
        ui_draw_text(cr, w, t, left - 6, TY(v) + 3.5, 10.5, 0.55, 1.0);
    }
    if (ui_viewing_today(state)) {
        GDateTime *now = g_date_time_new_now_local();
        const int nm = g_date_time_get_hour(now) * 60 + g_date_time_get_minute(now);
        g_date_time_unref(now);
        const double dashes[] = {3.0, 3.0};
        cairo_set_dash(cr, dashes, 2, 0);
        cairo_set_source_rgba(cr, col[0], col[1], col[2], 0.55);
        cairo_move_to(cr, round(TX(nm)) + 0.5, top);
        cairo_line_to(cr, round(TX(nm)) + 0.5, top + ph);
        cairo_stroke(cr);
        cairo_set_dash(cr, NULL, 0, 0);
    }

    cairo_save(cr);
    cairo_rectangle(cr, left, top - 4, pw, ph + 4);
    cairo_clip(cr);
    if (series == UI_SERIES_STEPS) {
        const double bw = pw / TREND_BUCKETS;
        cairo_set_source_rgb(cr, col[0], col[1], col[2]);
        for (int b = 0; b < TREND_BUCKETS; b++) {
            if (buckets[b] <= 0) {
                continue;
            }
            const double y = TY(buckets[b]);
            ui_rounded_rect(cr, left + b * bw + 1.5, y, MAX(bw - 3.0, 1.0), top + ph - y + 3, 3.0);
            cairo_fill(cr);
        }
    } else {
        /* Soft fill under each continuous segment, then the line on top. */
        cairo_pattern_t *grad = cairo_pattern_create_linear(0, top, 0, top + ph);
        cairo_pattern_add_color_stop_rgba(grad, 0, col[0], col[1], col[2], 0.26);
        cairo_pattern_add_color_stop_rgba(grad, 1, col[0], col[1], col[2], 0.0);
        int seg_start = -1, prev = -1;
        for (int m = 0; m <= UI_DAY_MINUTES; m++) {
            const gboolean has = m < UI_DAY_MINUTES && values[m] >= 0;
            if (m < UI_DAY_MINUTES && !has) {
                continue; /* minutes without a reading are normal between samples */
            }
            if (has && prev >= 0 && m - prev <= TREND_MAX_GAP_MIN) {
                prev = m;
                continue;
            }
            if (seg_start >= 0 && prev > seg_start) {
                cairo_move_to(cr, TX(seg_start), top + ph);
                for (int k = seg_start; k <= prev; k++) {
                    if (values[k] >= 0) {
                        cairo_line_to(cr, TX(k), TY(values[k]));
                    }
                }
                cairo_line_to(cr, TX(prev), top + ph);
                cairo_close_path(cr);
                cairo_set_source(cr, grad);
                cairo_fill(cr);
            }
            if (has) {
                seg_start = m;
                prev = m;
            }
        }
        cairo_pattern_destroy(grad);

        cairo_set_source_rgb(cr, col[0], col[1], col[2]);
        cairo_set_line_width(cr, 2.0);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        prev = -1;
        for (int m = 0; m < UI_DAY_MINUTES; m++) {
            if (values[m] < 0) {
                continue;
            }
            if (prev >= 0 && m - prev <= TREND_MAX_GAP_MIN) {
                cairo_line_to(cr, TX(m), TY(values[m]));
            } else {
                cairo_move_to(cr, TX(m), TY(values[m]));
                cairo_line_to(cr, TX(m) + 0.01, TY(values[m])); /* an isolated reading shows as a dot */
            }
            prev = m;
        }
        cairo_stroke(cr);
    }
    cairo_restore(cr);

    if (dash->trend_hover_x >= left && dash->trend_hover_x <= left + pw) {
        const int hm = CLAMP((int)((dash->trend_hover_x - left) / pw * UI_DAY_MINUTES), 0, UI_DAY_MINUTES - 1);
        int at = -1, v = -1, span = 1;
        if (series == UI_SERIES_STEPS) {
            const int b = CLAMP(hm / TREND_STEP_BUCKET_MIN, 0, TREND_BUCKETS - 1);
            if (buckets[b] > 0) {
                at = b * TREND_STEP_BUCKET_MIN;
                v = buckets[b];
                span = TREND_STEP_BUCKET_MIN;
            }
        } else {
            for (int d = 0; d <= 30 && at < 0; d++) {
                if (hm - d >= 0 && values[hm - d] >= 0) {
                    at = hm - d;
                } else if (hm + d < UI_DAY_MINUTES && values[hm + d] >= 0) {
                    at = hm + d;
                }
            }
            if (at >= 0) {
                v = values[at];
            }
        }
        if (at >= 0) {
            const double x = span > 1 ? TX(at + span / 2.0) : TX(at);
            ui_cairo_set_fg(cr, w, 0.35);
            cairo_move_to(cr, round(x) + 0.5, top);
            cairo_line_to(cr, round(x) + 0.5, top + ph);
            cairo_stroke(cr);
            if (series != UI_SERIES_STEPS) {
                cairo_set_source_rgb(cr, col[0], col[1], col[2]);
                cairo_arc(cr, x, TY(v), 4.5, 0, 2 * G_PI);
                cairo_fill(cr);
            }
            gchar vt[48], c0[8], c1[8], tip[96];
            trend_format(series, v, vt, sizeof(vt));
            ui_format_clock(at, c0, sizeof(c0));
            if (span > 1) {
                ui_format_clock(at + span, c1, sizeof(c1));
                g_snprintf(tip, sizeof(tip), "%s–%s   %s", c0, c1, vt);
            } else {
                g_snprintf(tip, sizeof(tip), "%s   %s", c0, vt);
            }
            ui_draw_tooltip(cr, w, tip, x, top, left + pw);
        }
    }
#undef TX
#undef TY
    g_free(values);
}

static void trend_set_stat(SalusDashboardUi *dash, int i, const char *value, const char *caption) {
    gtk_label_set_text(GTK_LABEL(dash->trend_stat_value[i]), value);
    gtk_label_set_text(GTK_LABEL(dash->trend_stat_caption[i]), caption);
    gtk_widget_set_opacity(gtk_widget_get_parent(dash->trend_stat_value[i]), caption[0] != '\0' ? 1.0 : 0.0);
}

static void trend_update_stats(AppState *state, const SalusDayHealthAgg *agg) {
    SalusDashboardUi *dash = state->dash_ui;
    const TrendSpec *spec = &trend_specs[dash->trend_metric];
    int *values = g_new(int, UI_DAY_MINUTES);
    gchar *key = ui_day_key(ui_view_date(state));
    const guint n = ui_series_collect(state, key, spec->series, values);
    g_free(key);

    long sum = 0;
    int vmin = G_MAXINT, vmax = 0;
    for (int m = 0; m < UI_DAY_MINUTES; m++) {
        if (values[m] >= 0) {
            sum += values[m];
            vmin = MIN(vmin, values[m]);
            vmax = MAX(vmax, values[m]);
        }
    }
    const SalusDailySummaryRow *ds = agg->has_summary ? &agg->summary : NULL;
    gchar a[32], b[32], c[32], d[32];
    for (int i = 0; i < 4; i++) {
        trend_set_stat(dash, i, "", "");
    }
    if (spec->series == UI_SERIES_STEPS) {
        if (n > 0) {
            int buckets[TREND_BUCKETS], bmax = 0, best = 0;
            trend_step_buckets(values, buckets, &bmax);
            for (int k = 0; k < TREND_BUCKETS; k++) {
                if (buckets[k] > buckets[best]) {
                    best = k;
                }
            }
            ui_format_thousands(sum, a, sizeof(a));
            trend_set_stat(dash, 0, a, "Recorded steps");
            gchar c0[8], c1[8];
            ui_format_clock(best * TREND_STEP_BUCKET_MIN, c0, sizeof(c0));
            ui_format_clock((best + 1) * TREND_STEP_BUCKET_MIN, c1, sizeof(c1));
            g_snprintf(b, sizeof(b), "%s–%s", c0, c1);
            trend_set_stat(dash, 1, b, "Most active");
        }
        if (ds != NULL && ds->steps > 0) {
            ui_format_thousands(ds->steps, c, sizeof(c));
            trend_set_stat(dash, 2, c, "Band daily total");
        }
        gtk_label_set_text(GTK_LABEL(dash->trend_subtitle), "Steps per half hour");
    } else if (n > 0) {
        const int avg = (int)((sum + n / 2) / n);
        const char *unit = spec->series == UI_SERIES_HR ? " bpm" : spec->series == UI_SERIES_SPO2 ? "%" : "";
        g_snprintf(a, sizeof(a), "%d%s", avg, unit);
        g_snprintf(b, sizeof(b), "%d%s", vmin, unit);
        g_snprintf(c, sizeof(c), "%d%s", vmax, unit);
        trend_set_stat(dash, 0, a, "Average");
        trend_set_stat(dash, 1, b, "Lowest");
        trend_set_stat(dash, 2, c, "Highest");
        if (spec->series == UI_SERIES_HR && ds != NULL && ds->hr_resting > 0) {
            g_snprintf(d, sizeof(d), "%d bpm", ds->hr_resting);
            trend_set_stat(dash, 3, d, "Resting");
        } else if (spec->series == UI_SERIES_STRESS) {
            trend_set_stat(dash, 3, salus_pressure_level_label(avg), "Level");
        } else {
            g_snprintf(d, sizeof(d), "%u", n);
            trend_set_stat(dash, 3, d, "Readings");
        }
        gtk_label_set_text(GTK_LABEL(dash->trend_subtitle), "Hover the chart for individual readings");
    } else {
        gtk_label_set_text(GTK_LABEL(dash->trend_subtitle),
                           ds != NULL ? "No per-minute readings — showing the band's daily summary"
                                      : "Sync the band to fill this chart");
        if (ds != NULL) {
            if (spec->series == UI_SERIES_HR && ds->hr_avg > 0) {
                g_snprintf(a, sizeof(a), "%d bpm", ds->hr_avg);
                g_snprintf(b, sizeof(b), "%d bpm", ds->hr_min);
                g_snprintf(c, sizeof(c), "%d bpm", ds->hr_max);
                g_snprintf(d, sizeof(d), "%d bpm", ds->hr_resting);
                trend_set_stat(dash, 0, a, "Average");
                trend_set_stat(dash, 1, b, "Lowest");
                trend_set_stat(dash, 2, c, "Highest");
                trend_set_stat(dash, 3, d, "Resting");
            } else if (spec->series == UI_SERIES_SPO2 && salus_spo2_value_valid(ds->spo2_avg)) {
                g_snprintf(a, sizeof(a), "%d%%", ds->spo2_avg);
                g_snprintf(b, sizeof(b), "%d%%", ds->spo2_min);
                g_snprintf(c, sizeof(c), "%d%%", ds->spo2_max);
                trend_set_stat(dash, 0, a, "Average");
                trend_set_stat(dash, 1, b, "Lowest");
                trend_set_stat(dash, 2, c, "Highest");
            } else if (spec->series == UI_SERIES_STRESS && salus_stress_value_valid(ds->stress_avg)) {
                g_snprintf(a, sizeof(a), "%d", ds->stress_avg);
                g_snprintf(b, sizeof(b), "%d", ds->stress_min);
                g_snprintf(c, sizeof(c), "%d", ds->stress_max);
                trend_set_stat(dash, 0, a, "Average");
                trend_set_stat(dash, 1, b, "Lowest");
                trend_set_stat(dash, 2, c, "Highest");
            }
        }
    }
    g_free(values);
}

static void on_trend_motion(GtkEventControllerMotion *m, double x, double y, gpointer user_data) {
    (void)m;
    (void)y;
    SalusDashboardUi *dash = user_data;
    dash->trend_hover_x = x;
    gtk_widget_queue_draw(dash->trend_area);
}

static void on_trend_leave(GtkEventControllerMotion *m, gpointer user_data) {
    (void)m;
    SalusDashboardUi *dash = user_data;
    dash->trend_hover_x = -1;
    gtk_widget_queue_draw(dash->trend_area);
}

static GtkWidget *build_trend_card(AppState *state, SalusDashboardUi *dash) {
    GtkWidget *inner = NULL;
    GtkWidget *card = ui_card_new(&inner, 12);
    gtk_box_append(GTK_BOX(inner), ui_card_header("Through the Day", &dash->trend_subtitle, NULL));

    dash->trend_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(dash->trend_area, -1, 260);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(dash->trend_area), draw_trend, state, NULL);
    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_trend_motion), dash);
    g_signal_connect(motion, "leave", G_CALLBACK(on_trend_leave), dash);
    gtk_widget_add_controller(dash->trend_area, motion);
    gtk_box_append(GTK_BOX(inner), dash->trend_area);

    GtkWidget *stats = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_set_homogeneous(GTK_BOX(stats), TRUE);
    for (int i = 0; i < 4; i++) {
        GtkWidget *col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        dash->trend_stat_value[i] = ui_label("", "salus-stat-value numeric", 0.0f);
        dash->trend_stat_caption[i] = ui_label("", "caption dimmed", 0.0f);
        gtk_label_set_ellipsize(GTK_LABEL(dash->trend_stat_value[i]), PANGO_ELLIPSIZE_END);
        gtk_box_append(GTK_BOX(col), dash->trend_stat_value[i]);
        gtk_box_append(GTK_BOX(col), dash->trend_stat_caption[i]);
        gtk_box_append(GTK_BOX(stats), col);
    }
    gtk_box_append(GTK_BOX(inner), stats);
    return card;
}

/* One detail page, reused for every metric: pushed onto the navigation view by a tile. */
static AdwNavigationPage *build_metric_page(AppState *state, SalusDashboardUi *dash) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(box, "salus-page");
    gtk_box_append(GTK_BOX(box), ui_date_nav_new(state, dash));
    gtk_box_append(GTK_BOX(box), build_trend_card(state, dash));

    GtkWidget *scroll = ui_page_scroll(box, 960);

    GtkWidget *tb = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(tb), adw_header_bar_new());
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(tb), scroll);
    /* Kept alive between pushes; the navigation view drops its own reference on pop. */
    return g_object_ref_sink(adw_navigation_page_new_with_tag(tb, trend_specs[0].label, "metric"));
}

static void show_metric_page(AppState *state, UiSeries s) {
    SalusDashboardUi *dash = state->dash_ui;
    dash->trend_metric = s;
    dash->trend_hover_x = -1;
    adw_navigation_page_set_title(dash->metric_page, trend_specs[s].label);
    ui_today_refresh(state);
    AdwNavigationView *nav = ADW_NAVIGATION_VIEW(dash->nav_view);
    if (adw_navigation_view_get_visible_page(nav) != dash->metric_page) {
        adw_navigation_view_pop_to_tag(nav, "main");
        adw_navigation_view_push(nav, dash->metric_page);
    }
}

static void on_tile_clicked(GtkButton *b, gpointer user_data) {
    AppState *state = user_data;
    const char *id = g_object_get_data(G_OBJECT(b), "salus-tile");
    for (int i = 0; i < UI_SERIES_COUNT; i++) {
        if (g_strcmp0(id, trend_specs[i].name) == 0) {
            show_metric_page(state, trend_specs[i].series);
            return;
        }
    }
    if (g_strcmp0(id, "sleep") == 0) {
        ui_show_page(state, "sleep");
    } else if (g_strcmp0(id, "weight") == 0) {
        ui_show_weight_dialog(state);
    }
}

GtkWidget *ui_today_page_new(AppState *state, SalusDashboardUi *dash) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(box, "salus-page");
    gtk_box_append(GTK_BOX(box), ui_date_nav_new(state, dash));
    gtk_box_append(GTK_BOX(box), build_activity_card(state, dash));

    GtkWidget *flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(flow), TRUE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(flow), 2);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow), 4);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(flow), 12);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(flow), 12);

    tile_build(&dash->tile_hr, state, "hr", "Heart Rate", "salus-heart-symbolic", UI_COLOR_HR, TRUE, UI_SERIES_HR);
    tile_build(&dash->tile_spo2, state, "spo2", "SpO₂", "salus-drop-symbolic", UI_COLOR_SPO2, TRUE,
               UI_SERIES_SPO2);
    tile_build(&dash->tile_stress, state, "stress", "Stress", "salus-stress-symbolic", UI_COLOR_STRESS, TRUE,
               UI_SERIES_STRESS);
    tile_build(&dash->tile_sleep, state, "sleep", "Sleep", "salus-moon-symbolic", UI_COLOR_SLEEP, TRUE, -1);
    tile_build(&dash->tile_vitality, state, "vitality", "Vitality", "salus-bolt-symbolic", UI_COLOR_VITALITY, FALSE,
               -1);
    tile_build(&dash->tile_weight, state, "weight", "Weight", "salus-scale-symbolic", UI_COLOR_WEIGHT, TRUE, -1);
    tile_build(&dash->tile_cycles, state, "cycles", "Cycle", "salus-cycle-symbolic", UI_COLOR_CYCLES, FALSE, -1);
    UiTile *tiles[] = {&dash->tile_hr,       &dash->tile_spo2,   &dash->tile_stress, &dash->tile_sleep,
                       &dash->tile_vitality, &dash->tile_weight, &dash->tile_cycles};
    for (size_t i = 0; i < G_N_ELEMENTS(tiles); i++) {
        gtk_widget_set_size_request(tiles[i]->root, 150, -1);
        gtk_flow_box_append(GTK_FLOW_BOX(flow), tiles[i]->root);
    }
    gtk_box_append(GTK_BOX(box), flow);
    dash->metric_page = build_metric_page(state, dash);

    GtkWidget *scroll = ui_page_scroll(box, 960);
    return scroll;
}

/* The day's most recent per-minute reading of a series (synced, manual or live-merged); -1 if none. */
static int last_reading(AppState *state, const gchar *day_key, UiSeries series, int *minute_out) {
    int *values = g_new(int, UI_DAY_MINUTES);
    int v = -1;
    *minute_out = -1;
    if (ui_series_collect(state, day_key, series, values) > 0) {
        for (int m = UI_DAY_MINUTES - 1; m >= 0; m--) {
            if (values[m] >= 0) {
                v = values[m];
                *minute_out = m;
                break;
            }
        }
    }
    g_free(values);
    return v;
}

/* "Measured at 14:32" (or "… · Relaxed"). */
static void last_reading_caption(int minute, const char *extra, gchar *buf, gsize len) {
    gchar clock[8];
    ui_format_clock(minute, clock, sizeof(clock));
    if (extra != NULL) {
        g_snprintf(buf, len, "At %s · %s", clock, extra);
    } else {
        g_snprintf(buf, len, "Measured at %s", clock);
    }
}

static gboolean live_tiles(AppState *state) {
    return ui_is_connected(state) && state->auth_initialized && ui_viewing_today(state);
}

void ui_today_refresh(AppState *state) {
    SalusDashboardUi *dash = state->dash_ui;
    if (dash == NULL || dash->rings == NULL) {
        return;
    }
    gchar *day_key = ui_day_key(ui_view_date(state));
    SalusDayHealthAgg agg;
    salus_health_aggregate_day(state, day_key, &agg);
    const gboolean live = live_tiles(state);
    gchar buf[64], sub[96];

    /* Activity rings */
    const long steps = (live && state->has_live_steps) ? (long)state->live_steps : agg.steps;
    /* The band's calories (totalactiveCalories) aren't in the live stats; only synced data has them. */
    const long cal = agg.calories;
    const guint goal_active = state->goal_moving > 0 ? state->goal_moving : salus_activity_goal_default();
    const guint active = salus_count_activity_minutes_from_timeline(state, day_key);
    set_ring_value(dash->ring_steps, steps, GOAL_STEPS, "");
    set_ring_value(dash->ring_calories, cal, GOAL_CALORIES, " kcal");
    set_ring_value(dash->ring_active, active > 0 ? (long)active : -1, goal_active, " min");
    dash->ring_ratio[0] = steps > 0 ? (double)steps / GOAL_STEPS : 0;
    dash->ring_ratio[1] = cal > 0 ? (double)cal / GOAL_CALORIES : 0;
    dash->ring_ratio[2] = active > 0 ? (double)active / goal_active : 0;
    gtk_widget_set_visible(dash->ring_badge, live && state->has_live_steps);

    /* Heart rate, SpO2, stress: the live reading while connected, else the day's last one. */
    {
        int at = -1;
        const int last = last_reading(state, day_key, UI_SERIES_HR, &at);
        if (live && state->has_live_hr) {
            g_snprintf(buf, sizeof(buf), "%u", state->live_hr);
            const int zone = salus_hr_zone_index((int)state->live_hr, (int)state->profile_max_hr);
            g_snprintf(sub, sizeof(sub), "Live%s%s", zone > 0 ? " · " : "", salus_hr_zone_label(zone));
            tile_set(&dash->tile_hr, buf, "bpm", sub);
        } else if (last > 0) {
            g_snprintf(buf, sizeof(buf), "%d", last);
            last_reading_caption(at, NULL, sub, sizeof(sub));
            tile_set(&dash->tile_hr, buf, "bpm", sub);
        } else if (agg.has_summary && agg.summary.hr_avg > 0) {
            g_snprintf(buf, sizeof(buf), "%d", agg.summary.hr_avg);
            tile_set(&dash->tile_hr, buf, "bpm", "Daily average");
        } else {
            tile_set(&dash->tile_hr, "—", NULL, "No readings");
        }
    }
    {
        int at = -1;
        const int last = last_reading(state, day_key, UI_SERIES_SPO2, &at);
        if (last > 0) {
            g_snprintf(buf, sizeof(buf), "%d", last);
            last_reading_caption(at, NULL, sub, sizeof(sub));
            tile_set(&dash->tile_spo2, buf, "%", sub);
        } else if (agg.has_summary && salus_spo2_value_valid(agg.summary.spo2_avg)) {
            g_snprintf(buf, sizeof(buf), "%d", agg.summary.spo2_avg);
            tile_set(&dash->tile_spo2, buf, "%", "Daily average");
        } else {
            tile_set(&dash->tile_spo2, "—", NULL, "No readings");
        }
    }
    {
        int at = -1;
        const int last = last_reading(state, day_key, UI_SERIES_STRESS, &at);
        if (last > 0) {
            g_snprintf(buf, sizeof(buf), "%d", last);
            last_reading_caption(at, salus_pressure_level_label(last), sub, sizeof(sub));
            tile_set(&dash->tile_stress, buf, NULL, sub);
        } else if (agg.has_summary && salus_stress_value_valid(agg.summary.stress_avg)) {
            g_snprintf(buf, sizeof(buf), "%d", agg.summary.stress_avg);
            g_snprintf(sub, sizeof(sub), "Daily avg · %s", salus_pressure_level_label(agg.summary.stress_avg));
            tile_set(&dash->tile_stress, buf, NULL, sub);
        } else {
            tile_set(&dash->tile_stress, "—", NULL, "No readings");
        }
    }

    /* Sleep: the night that ended on this day */
    {
        UiNight *n = dash->night;
        ui_night_collect(state, ui_view_date(state), n);
        if (n->total_asleep > 0) {
            g_snprintf(buf, sizeof(buf), "%dh %02dm", n->total_asleep / 60, n->total_asleep % 60);
            if (n->first >= 0) {
                gchar c0[8], c1[8];
                ui_format_clock(UI_NIGHT_START_MIN + n->first, c0, sizeof(c0));
                ui_format_clock(UI_NIGHT_START_MIN + n->last + 1, c1, sizeof(c1));
                g_snprintf(sub, sizeof(sub), "%s – %s", c0, c1);
            } else {
                g_snprintf(sub, sizeof(sub), "Asleep");
            }
            tile_set(&dash->tile_sleep, buf, NULL, sub);
        } else {
            tile_set(&dash->tile_sleep, "—", NULL, "No sleep recorded");
        }
    }

    /* Vitality */
    if (agg.has_summary && agg.summary.has_training_vitality) {
        g_snprintf(buf, sizeof(buf), "%d", agg.summary.vitality_current);
        g_snprintf(sub, sizeof(sub), "7-day score · goal %d", GOAL_VITALITY);
        tile_set(&dash->tile_vitality, buf, NULL, sub);
    } else {
        tile_set(&dash->tile_vitality, "—", NULL, "From the band's daily summary");
    }

    /* Weight */
    {
        guint last_date = 0;
        const float kg = salus_weight_log_latest_kg(state, &last_date);
        if (kg > 0.0f) {
            g_snprintf(buf, sizeof(buf), "%.1f", kg);
            const float bmi = salus_profile_bmi_kg_m2(kg, state->profile_height_cm);
            GString *s = g_string_new(NULL);
            if (bmi > 0.0f) {
                g_string_append_printf(s, "BMI %.1f", bmi);
            }
            if (state->weight_log != NULL && state->weight_log->len > 1) {
                const SalusWeightEntry *prev = &g_array_index(state->weight_log, SalusWeightEntry, 1);
                g_string_append_printf(s, "%s%+.1f kg", s->len ? " · " : "", kg - prev->kg);
            }
            if (s->len == 0) {
                g_string_append(s, "Tap to update");
            }
            tile_set(&dash->tile_weight, buf, "kg", s->str);
            g_string_free(s, TRUE);
        } else {
            tile_set(&dash->tile_weight, "—", NULL, "Tap to log your weight");
        }
    }

    /* Cycles */
    GtkWidget *cycles_child = gtk_widget_get_parent(dash->tile_cycles.root);
    if (salus_profile_shows_cycles(state->profile_gender)) {
        gtk_widget_set_visible(cycles_child, TRUE);
        const int day = salus_cycle_day_of_period(ui_view_date(state), state->profile_last_period_yyyymmdd,
                                                   state->profile_cycle_length_days);
        if (day > 0) {
            g_snprintf(buf, sizeof(buf), "%d", day);
            tile_set(&dash->tile_cycles, buf, NULL, salus_cycle_phase_label(day, state->profile_cycle_length_days));
        } else {
            tile_set(&dash->tile_cycles, "—", NULL, "Set last_period in config");
        }
    } else {
        gtk_widget_set_visible(cycles_child, FALSE);
    }

    trend_update_stats(state, &agg);
    g_free(day_key);
    ui_today_redraw(state);
}

void ui_today_redraw(AppState *state) {
    SalusDashboardUi *dash = state->dash_ui;
    if (dash == NULL || dash->trend_area == NULL) {
        return;
    }
    gtk_widget_queue_draw(dash->trend_area);
    gtk_widget_queue_draw(dash->rings);
    UiTile *tiles[] = {&dash->tile_hr, &dash->tile_spo2, &dash->tile_stress};
    for (size_t i = 0; i < G_N_ELEMENTS(tiles); i++) {
        if (tiles[i]->spark != NULL) {
            gtk_widget_queue_draw(tiles[i]->spark);
        }
    }
}

/* ======================================================================================== */
/* Public API used by the backend                                                           */
/* ======================================================================================== */

static gboolean dashboard_refresh_idle(gpointer user_data) {
    AppState *state = user_data;
    state->dashboard_refresh_pending = FALSE;
    salus_dashboard_refresh(state);
    return G_SOURCE_REMOVE;
}

void salus_dashboard_schedule_refresh(AppState *state) {
    if (state == NULL || state->dash_ui == NULL || state->dashboard_refresh_pending) {
        return;
    }
    state->dashboard_refresh_pending = TRUE;
    g_idle_add(dashboard_refresh_idle, state);
}

void salus_dashboard_refresh(AppState *state) {
    if (state == NULL || state->dash_ui == NULL || state->dash_ui->rings == NULL) {
        return;
    }
    dashboard_update_date_navs(state);
    ui_today_refresh(state);
    mi_sleep_refresh(state);
    ui_workouts_refresh_training(state);
    salus_dashboard_update_connection(state);
}

void salus_dashboard_redraw_charts(AppState *state) {
    /* Called for every merged sample batch: coalesce into one refresh on idle. */
    salus_dashboard_schedule_refresh(state);
}

static gchar *device_status_label_copy(const gchar *kind, const gchar *value) {
    if (value == NULL || value[0] == '\0') {
        return NULL;
    }
    if (g_strcmp0(kind, "charging") == 0) {
        if (g_strcmp0(value, "yes") == 0) {
            return g_strdup("charging");
        }
        if (g_strcmp0(value, "no") == 0) {
            return NULL;
        }
    }
    return g_strdup(value);
}

void salus_dashboard_update_device_state(AppState *state,
                                          const gchar *charging,
                                          const gchar *wearing,
                                          const gchar *sleep) {
    if (state == NULL) {
        return;
    }
    if (wearing != NULL) {
        g_free(state->device_status_wearing);
        state->device_status_wearing = device_status_label_copy("wearing", wearing);
    }
    if (charging != NULL) {
        g_free(state->device_status_charging);
        state->device_status_charging = device_status_label_copy("charging", charging);
    }
    if (sleep != NULL) {
        g_free(state->device_status_sleep);
        state->device_status_sleep = device_status_label_copy("sleep", sleep);
    }
    ui_device_refresh(state);
}

void salus_dashboard_set_notification_status(AppState *state, const char *msg) {
    if (state == NULL || state->dash_ui == NULL || state->dash_ui->dev_notif_switch == NULL) {
        return;
    }
    adw_action_row_set_subtitle(ADW_ACTION_ROW(state->dash_ui->dev_notif_switch), msg != NULL ? msg : "");
}
