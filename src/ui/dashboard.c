#include "miwear/ui/dashboard.h"
#include "miwear/activity/workout_summary.h"
#include "miwear/profile.h"
#include "miwear/ui/health_data.h"
#include "miwear/weight_log.h"
#include "miwear/activity/activity.h"
#include "miwear/spp/fitness_sync.h"
#include "miwear/notifications/notifications.h"
#include "miwear/spp/weather.h"
#include "miwear/find_phone.h"
#include "miwear/spp/wire_internal.h"

#include <adwaita.h>
#include <cairo.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef MIWEAR_ICONS_DIR
#define MIWEAR_ICONS_DIR "src/resources/icons"
#endif

#define GOAL_STEPS 10000
#define GOAL_CALORIES 400
#define GOAL_SLEEP_MIN 480
#define GOAL_SPO2 100
#define GOAL_VITALITY 100
#define GOAL_ENERGY 100

struct MiwearDashboardUi {
    GtkWidget *date_label;
    GtkWidget *status_chip;
    GtkWidget *battery_chip;
    GtkWidget *card_steps;
    GtkWidget *card_calories;
    GtkWidget *card_hr;
    GtkWidget *card_spo2;
    GtkWidget *card_stress;
    GtkWidget *card_sleep;
    GtkWidget *card_standing;
    GtkWidget *card_vitality;
    GtkWidget *card_energy;
    GtkWidget *card_weight;
    GtkWidget *card_bp;
    GtkWidget *card_cycles;
    GtkWidget *sleep_hypnogram;
    GtkWidget *sleep_stats;
    GtkWidget *sport_list;
    GtkWidget *device_log_scroll;
    GtkWidget *btn_sync;
    GtkWidget *btn_weather;
    GtkWidget *weather_status;
    GtkWidget *switch_notifications;
    GtkWidget *btn_notif_test;
    GtkWidget *notification_status;
    GtkWidget *btn_find_band;
    GtkWidget *btn_find_phone_stop;
    GtkWidget *find_phone_status;
    GtkWidget *nav_icons[4];
    guint nav_icon_count;
    double ui_scale;
    gboolean ui_scale_ready;
};

static GtkCssProvider *g_dashboard_css_provider;

static int ui_px(double base, double scale) {
    return (int)(base * scale + 0.5);
}

static double viewport_ui_scale(int width, int height) {
    const int w = width > 0 ? width : 420;
    const int h = height > 0 ? height : 780;
    const double ref_area = 420.0 * 780.0;
    double scale = sqrt(((double)w * (double)h) / ref_area);
    const double narrow = (double)w / 420.0;
    const double tall = (double)h / 780.0;
    scale = (scale * 0.65) + (narrow * 0.2) + (tall * 0.15);
    return CLAMP(scale, 0.85, 1.32);
}

static void dashboard_css_load(double scale) {
    if (g_dashboard_css_provider == NULL) {
        return;
    }
    if (scale <= 0.0) {
        scale = 1.0;
    }

    GString *css = g_string_new(NULL);
    g_string_append_printf(
        css,
        ".miwear-app { font-size: %dpx; }"
        ".mi-card { border-radius: %dpx; min-height: %dpx; }"
        ".mi-card-content { padding: %dpx; }"
        ".mi-card-title { opacity: 0.65; font-size: %dpx; }"
        ".mi-card-value { font-size: %dpx; font-weight: 700; }"
        ".mi-card-sub { opacity: 0.7; font-size: %dpx; }"
        ".mi-header { font-size: %dpx; font-weight: 600; }"
        ".mi-chip { font-size: %dpx; padding: %dpx %dpx; border-radius: 999px;"
        " background: alpha(currentColor, 0.08); }"
        ".mi-status-footer { font-size: %dpx; }"
        ".mi-bottom-nav {"
        "  min-height: %dpx;"
        "  padding: %dpx %dpx %dpx;"
        "  border-top: 1px solid alpha(currentColor, 0.1);"
        "  background-color: @window_bg_color;"
        "}"
        ".mi-nav-btn { border-radius: %dpx; padding: %dpx %dpx; }"
        ".mi-nav-btn label { font-size: %dpx; opacity: 0.82; }"
        ".mi-nav-btn:checked label { opacity: 1; font-weight: 600; }"
        ".mi-nav-btn:checked { background: alpha(currentColor, 0.1); }",
        ui_px(13.0, scale),
        ui_px(16.0, scale),
        ui_px(108.0, scale),
        ui_px(14.0, scale),
        ui_px(13.6, scale),
        ui_px(24.8, scale),
        ui_px(12.8, scale),
        ui_px(17.6, scale),
        ui_px(12.0, scale),
        ui_px(4.0, scale),
        ui_px(10.0, scale),
        ui_px(11.0, scale),
        ui_px(68.0, scale),
        ui_px(8.0, scale),
        ui_px(10.0, scale),
        ui_px(12.0, scale),
        ui_px(12.0, scale),
        ui_px(6.0, scale),
        ui_px(4.0, scale),
        ui_px(11.5, scale));

    gtk_css_provider_load_from_string(g_dashboard_css_provider, css->str);
    g_string_free(css, TRUE);
}

static void ensure_dashboard_css_provider(GtkWidget *root) {
    if (g_dashboard_css_provider != NULL) {
        return;
    }
    g_dashboard_css_provider = gtk_css_provider_new();
    gtk_style_context_add_provider_for_display(
        gtk_widget_get_display(root),
        GTK_STYLE_PROVIDER(g_dashboard_css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

static void miwear_dashboard_apply_ui_scale(MiwearDashboardUi *dash, double scale) {
    if (dash == NULL) {
        return;
    }
    scale = CLAMP(scale, 0.85, 1.32);
    if (dash->ui_scale_ready && fabs(dash->ui_scale - scale) < 0.025) {
        return;
    }
    dash->ui_scale = scale;
    dash->ui_scale_ready = TRUE;

    dashboard_css_load(scale);

    if (dash->sleep_hypnogram != NULL) {
        gtk_widget_set_size_request(dash->sleep_hypnogram, -1, ui_px(220.0, scale));
        gtk_widget_queue_draw(dash->sleep_hypnogram);
    }
    for (guint i = 0; i < dash->nav_icon_count; i++) {
        if (dash->nav_icons[i] != NULL) {
            gtk_image_set_pixel_size(GTK_IMAGE(dash->nav_icons[i]), ui_px(22.0, scale));
        }
    }
}

double miwear_dashboard_ui_scale(MiwearDashboardUi *dash) {
    if (dash == NULL || !dash->ui_scale_ready || dash->ui_scale <= 0.0) {
        return 1.0;
    }
    return dash->ui_scale;
}

void miwear_dashboard_apply_viewport(AppState *state, int width, int height) {
    if (state == NULL || state->dash_ui == NULL) {
        return;
    }
    const double before = miwear_dashboard_ui_scale(state->dash_ui);
    miwear_dashboard_apply_ui_scale(state->dash_ui, viewport_ui_scale(width, height));
    const double after = miwear_dashboard_ui_scale(state->dash_ui);
    if (fabs(before - after) >= 0.025 && state->recorded_chart != NULL) {
        gtk_widget_queue_draw(state->recorded_chart);
    }
}

static void install_dashboard_css(GtkWidget *root, MiwearDashboardUi *dash) {
    ensure_dashboard_css_provider(root);
    miwear_dashboard_apply_ui_scale(dash, 1.0);
}

MiwearDashboardUi *miwear_dashboard_ui_new(void) {
    MiwearDashboardUi *dash = g_new0(MiwearDashboardUi, 1);
    dash->ui_scale = 1.0;
    return dash;
}

void miwear_dashboard_ui_free(MiwearDashboardUi *dash) {
    g_free(dash);
}

static double metric_ratio(gdouble value, gdouble goal) {
    if (goal <= 0.0 || value < 0.0) {
        return -1.0;
    }
    return CLAMP(value / goal, 0.0, 1.0);
}

static void gauge_clip_rounded(cairo_t *cr, double w, double h, double r) {
    cairo_new_path(cr);
    cairo_arc(cr, r, r, r, G_PI, 3.0 * G_PI / 2.0);
    cairo_arc(cr, w - r, r, r, 3.0 * G_PI / 2.0, 0.0);
    cairo_arc(cr, w - r, h - r, r, 0.0, G_PI / 2.0);
    cairo_arc(cr, r, h - r, r, G_PI / 2.0, G_PI);
    cairo_close_path(cr);
    cairo_clip(cr);
}

static double gauge_progress_get(GtkWidget *gauge) {
    const double *stored = g_object_get_data(G_OBJECT(gauge), "progress-value");
    return stored != NULL ? *stored : -1.0;
}

static void gauge_progress_set(GtkWidget *gauge, double progress) {
    double *stored = g_object_get_data(G_OBJECT(gauge), "progress-value");
    if (stored == NULL) {
        stored = g_new(double, 1);
        g_object_set_data_full(G_OBJECT(gauge), "progress-value", stored, (GDestroyNotify)g_free);
    }
    *stored = progress;
    gtk_widget_queue_draw(gauge);
}

static void draw_card_gauge(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
    (void)user_data;
    if (width <= 0 || height <= 0) {
        return;
    }
    const double progress = gauge_progress_get(GTK_WIDGET(area));
    const double w = (double)width;
    const double h = (double)height;
    const double radius = 16.0;

    cairo_save(cr);
    gauge_clip_rounded(cr, w, h, radius);

    cairo_set_source_rgba(cr, 0.45, 0.45, 0.5, 0.08);
    cairo_paint(cr);

    if (progress >= 0.0 && progress > 0.001) {
        const double fill_h = h * progress;
        cairo_rectangle(cr, 0.0, h - fill_h, w, fill_h);
        /* Cairo draw path: fixed accent tint (avoids deprecated GtkStyleContext). */
        cairo_set_source_rgba(cr, 0.26, 0.52, 0.96, 0.38);
        cairo_fill(cr);
    }

    cairo_restore(cr);

    cairo_new_path(cr);
    cairo_set_source_rgba(cr, 0.45, 0.45, 0.5, 0.14);
    cairo_set_line_width(cr, 1.0);
    cairo_arc(cr, radius, radius, radius, G_PI, 3.0 * G_PI / 2.0);
    cairo_arc(cr, w - radius, radius, radius, 3.0 * G_PI / 2.0, 0.0);
    cairo_arc(cr, w - radius, h - radius, radius, 0.0, G_PI / 2.0);
    cairo_arc(cr, radius, h - radius, radius, G_PI / 2.0, G_PI);
    cairo_close_path(cr);
    cairo_stroke(cr);
}

static GtkWidget *make_gauge_card(const char *title, const char *value, const char *subtitle) {
    GtkWidget *overlay = gtk_overlay_new();
    gtk_widget_add_css_class(overlay, "mi-card");
    gtk_widget_set_overflow(overlay, GTK_OVERFLOW_HIDDEN);

    GtkWidget *gauge = gtk_drawing_area_new();
    gtk_widget_set_can_focus(gauge, FALSE);
    gtk_widget_set_hexpand(gauge, TRUE);
    gtk_widget_set_vexpand(gauge, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(gauge), draw_card_gauge, NULL, NULL);
    gauge_progress_set(gauge, -1.0);
    gtk_overlay_set_child(GTK_OVERLAY(overlay), gauge);

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(content, "mi-card-content");
    gtk_widget_set_hexpand(content, TRUE);
    gtk_widget_set_vexpand(content, TRUE);

    GtkWidget *t = gtk_label_new(title);
    gtk_widget_add_css_class(t, "mi-card-title");
    gtk_label_set_xalign(GTK_LABEL(t), 0.0f);
    GtkWidget *v = gtk_label_new(value);
    gtk_widget_add_css_class(v, "mi-card-value");
    gtk_label_set_xalign(GTK_LABEL(v), 0.0f);
    g_object_set_data(G_OBJECT(overlay), "value-label", v);
    g_object_set_data(G_OBJECT(overlay), "gauge-area", gauge);
    gtk_box_append(GTK_BOX(content), t);
    gtk_box_append(GTK_BOX(content), v);
    if (subtitle != NULL && subtitle[0] != '\0') {
        GtkWidget *s = gtk_label_new(subtitle);
        gtk_widget_add_css_class(s, "mi-card-sub");
        gtk_label_set_xalign(GTK_LABEL(s), 0.0f);
        g_object_set_data(G_OBJECT(overlay), "sub-label", s);
        gtk_box_append(GTK_BOX(content), s);
    }
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), content);
    return overlay;
}

static void ensure_device_display_name(AppState *state) {
    if (state == NULL || state->selected_address == NULL || state->selected_address[0] == '\0') {
        return;
    }
    if (state->device_display_name != NULL && state->device_display_name[0] != '\0') {
        return;
    }
    gchar *name = bluez_device_display_name(state->selected_address);
    g_free(state->device_display_name);
    if (name != NULL && name[0] != '\0') {
        state->device_display_name = name;
    } else {
        g_free(name);
        state->device_display_name = g_strdup(state->selected_address);
    }
}

static void refresh_device_status_chip(AppState *state) {
    MiwearDashboardUi *dash = state->dash_ui;
    if (dash == NULL || dash->status_chip == NULL) {
        return;
    }

    ensure_device_display_name(state);

    GString *s = g_string_new("Device: ");
    if (state->device_display_name != NULL && state->device_display_name[0] != '\0') {
        g_string_append(s, state->device_display_name);
    } else if (state->selected_address != NULL && state->selected_address[0] != '\0') {
        g_string_append(s, state->selected_address);
    } else {
        g_string_append(s, "—");
    }

    if (state->device_status_wearing != NULL) {
        g_string_append_printf(s, " · %s", state->device_status_wearing);
    }
    if (state->device_status_charging != NULL) {
        g_string_append_printf(s, " · %s", state->device_status_charging);
    }
    if (state->device_status_sleep != NULL) {
        g_string_append_printf(s, " · %s", state->device_status_sleep);
    }

    gtk_label_set_text(GTK_LABEL(dash->status_chip), s->str);
    g_string_free(s, TRUE);
}

typedef struct {
    GtkStack *stack;
    const char *page_name;
} BottomNavCtx;

static void on_bottom_nav_toggled(GtkToggleButton *button, gpointer user_data) {
    BottomNavCtx *ctx = user_data;
    if (!gtk_toggle_button_get_active(button) || ctx == NULL || ctx->stack == NULL) {
        return;
    }
    gtk_stack_set_visible_child_name(GTK_STACK(ctx->stack), ctx->page_name);
}

static GtkWidget *make_bottom_nav_button(MiwearDashboardUi *dash,
                                         GtkStack *stack,
                                         GtkToggleButton *group,
                                         const char *page_name,
                                         const char *title,
                                         const char *icon_file) {
    gchar *path = g_build_filename(MIWEAR_ICONS_DIR, icon_file, NULL);
    GtkWidget *btn = gtk_toggle_button_new();
    gtk_widget_add_css_class(btn, "mi-nav-btn");
    gtk_widget_set_hexpand(btn, TRUE);
    if (group != NULL) {
        gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(btn), group);
    }

    GtkWidget *col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *icon = gtk_image_new_from_file(path);
    g_free(path);
    const double icon_scale = dash != NULL && dash->ui_scale > 0.0 ? dash->ui_scale : 1.0;
    gtk_image_set_pixel_size(GTK_IMAGE(icon), ui_px(22.0, icon_scale));
    if (dash != NULL && dash->nav_icon_count < G_N_ELEMENTS(dash->nav_icons)) {
        dash->nav_icons[dash->nav_icon_count++] = icon;
    }
    GtkWidget *label = gtk_label_new(title);
    gtk_label_set_xalign(GTK_LABEL(label), 0.5f);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(col), icon);
    gtk_box_append(GTK_BOX(col), label);
    gtk_button_set_child(GTK_BUTTON(btn), col);

    BottomNavCtx *ctx = g_new(BottomNavCtx, 1);
    ctx->stack = stack;
    ctx->page_name = page_name;
    g_signal_connect(btn, "toggled", G_CALLBACK(on_bottom_nav_toggled), ctx);
    g_object_set_data_full(G_OBJECT(btn), "nav-ctx", ctx, g_free);
    return btn;
}

typedef struct {
    AppState *state;
    GtkWidget *window;
    GtkWidget *spin;
} WeightLogDialogCtx;

static gchar *format_yyyymmdd_short(guint yyyymmdd) {
    if (yyyymmdd < 20000101u) {
        return g_strdup("");
    }
    const guint y = yyyymmdd / 10000u;
    const guint m = (yyyymmdd / 100u) % 100u;
    const guint d = yyyymmdd % 100u;
    return g_strdup_printf("%04u-%02u-%02u", y, m, d);
}

static void on_weight_log_save(GtkWidget *button, gpointer user_data) {
    (void)button;
    WeightLogDialogCtx *ctx = user_data;
    const double kg = gtk_spin_button_get_value(GTK_SPIN_BUTTON(ctx->spin));
    if (miwear_weight_log_add_today(ctx->state, (float)kg)) {
        miwear_dashboard_refresh(ctx->state);
    }
    if (ctx->window != NULL) {
        gtk_window_destroy(GTK_WINDOW(ctx->window));
    }
}

static void on_weight_log_dialog_closed(GtkWindow *win, gpointer user_data) {
    g_free(user_data);
    (void)win;
}

static void show_weight_log_dialog(AppState *state) {
    if (state == NULL || state->window == NULL) {
        return;
    }

    WeightLogDialogCtx *ctx = g_new0(WeightLogDialogCtx, 1);
    ctx->state = state;

    GtkWidget *win = gtk_window_new();
    ctx->window = win;
    gtk_window_set_title(GTK_WINDOW(win), "Log weight");
    gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW(state->window));
    gtk_window_set_modal(GTK_WINDOW(win), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(win), 340, -1);

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(outer, 20);
    gtk_widget_set_margin_end(outer, 20);
    gtk_widget_set_margin_top(outer, 20);
    gtk_widget_set_margin_bottom(outer, 20);

    GtkWidget *hint = gtk_label_new("Enter your weight manually to track it over time.");
    gtk_label_set_wrap(GTK_LABEL(hint), TRUE);
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0f);
    gtk_box_append(GTK_BOX(outer), hint);

    GtkAdjustment *adj = gtk_adjustment_new(70.0, 30.0, 300.0, 0.1, 1.0, 0.0);
    guint last_date = 0;
    const float last_kg = miwear_weight_log_latest_kg(state, &last_date);
    if (last_kg > 0.0f) {
        gtk_adjustment_set_value(adj, last_kg);
    }
    ctx->spin = gtk_spin_button_new(adj, 0.1, 1);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(ctx->spin), TRUE);
    gtk_box_append(GTK_BOX(outer), ctx->spin);

    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(buttons, GTK_ALIGN_END);
    GtkWidget *cancel = gtk_button_new_with_label("Cancel");
    g_signal_connect_swapped(cancel, "clicked", G_CALLBACK(gtk_window_destroy), win);
    GtkWidget *save = gtk_button_new_with_label("Save");
    gtk_widget_add_css_class(save, "suggested-action");
    g_signal_connect(save, "clicked", G_CALLBACK(on_weight_log_save), ctx);
    gtk_box_append(GTK_BOX(buttons), cancel);
    gtk_box_append(GTK_BOX(buttons), save);
    gtk_box_append(GTK_BOX(outer), buttons);

    gtk_window_set_child(GTK_WINDOW(win), outer);
    g_signal_connect(win, "destroy", G_CALLBACK(on_weight_log_dialog_closed), ctx);
    gtk_window_present(GTK_WINDOW(win));
}

static void on_weight_card_pressed(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer user_data) {
    (void)gesture;
    (void)n_press;
    (void)x;
    (void)y;
    show_weight_log_dialog(user_data);
}

static void card_set_metric(GtkWidget *card, const char *value, const char *sub, double progress) {
    if (card == NULL) {
        return;
    }
    GtkWidget *v = g_object_get_data(G_OBJECT(card), "value-label");
    if (v != NULL && value != NULL) {
        gtk_label_set_text(GTK_LABEL(v), value);
    }
    GtkWidget *s = g_object_get_data(G_OBJECT(card), "sub-label");
    if (s != NULL && sub != NULL) {
        gtk_label_set_text(GTK_LABEL(s), sub);
    }
    GtkWidget *gauge = g_object_get_data(G_OBJECT(card), "gauge-area");
    if (gauge != NULL) {
        gauge_progress_set(gauge, progress);
    }
}

static void card_set(GtkWidget *card, const char *value, const char *sub) {
    card_set_metric(card, value, sub, -1.0);
}

static gchar *format_day_key(GDate *d) {
    GDateTime *dt = g_date_time_new_local(
        (gint)g_date_get_year(d),
        (gint)g_date_get_month(d),
        (gint)g_date_get_day(d),
        12, 0, 0.0);
    gchar *out = g_date_time_format(dt, "%Y-%m-%d");
    g_date_time_unref(dt);
    return out;
}

static GDate *current_view_date(AppState *state) {
    if (state->health_view_date == NULL) {
        GDateTime *now = g_date_time_new_now_local();
        state->health_view_date = g_date_new_dmy(
            (GDateDay)g_date_time_get_day_of_month(now),
            (GDateMonth)g_date_time_get_month(now),
            (GDateYear)g_date_time_get_year(now));
        g_date_time_unref(now);
    }
    return state->health_view_date;
}

static gboolean viewing_today(AppState *state) {
    GDateTime *now = g_date_time_new_now_local();
    GDate *today = g_date_new_dmy(
        (GDateDay)g_date_time_get_day_of_month(now),
        (GDateMonth)g_date_time_get_month(now),
        (GDateYear)g_date_time_get_year(now));
    g_date_time_unref(now);
    GDate *vd = current_view_date(state);
    const gboolean same = (g_date_compare(today, vd) == 0);
    g_date_free(today);
    return same;
}

static gboolean prefer_live_metric_tiles(AppState *state) {
    return state != NULL && state->auth_initialized && state->sock_fd >= 0 && viewing_today(state);
}

void miwear_dashboard_redraw_charts(AppState *state) {
    if (state == NULL) {
        return;
    }
    if (state->recorded_chart != NULL) {
        gtk_widget_queue_draw(state->recorded_chart);
    }
    MiwearDashboardUi *dash = state->dash_ui;
    if (dash != NULL && dash->sleep_hypnogram != NULL) {
        gtk_widget_queue_draw(dash->sleep_hypnogram);
    }
}

static void sync_selected_recorded_day(AppState *state, const gchar *day_key) {
    g_free(state->selected_recorded_day);
    state->selected_recorded_day = g_strdup(day_key);
}

static void refresh_day_dropdown_selection(AppState *state, const gchar *day_key) {
    if (state->recorded_day_dropdown == NULL || day_key == NULL) {
        return;
    }
    GtkStringList *sl = GTK_STRING_LIST(gtk_drop_down_get_model(GTK_DROP_DOWN(state->recorded_day_dropdown)));
    const guint n = g_list_model_get_n_items(G_LIST_MODEL(sl));
    for (guint i = 0; i < n; i++) {
        GtkStringObject *o = GTK_STRING_OBJECT(g_list_model_get_item(G_LIST_MODEL(sl), i));
        const char *s = gtk_string_object_get_string(o);
        g_object_unref(o);
        if (s != NULL && g_strcmp0(s, day_key) == 0) {
            gtk_drop_down_set_selected(GTK_DROP_DOWN(state->recorded_day_dropdown), i);
            return;
        }
    }
}

static gboolean dashboard_refresh_idle(gpointer user_data) {
    AppState *state = user_data;
    state->dashboard_refresh_pending = FALSE;
    miwear_dashboard_refresh(state);
    return G_SOURCE_REMOVE;
}

void miwear_dashboard_schedule_refresh(AppState *state) {
    if (state == NULL || state->dash_ui == NULL) {
        return;
    }
    if (state->dashboard_refresh_pending) {
        return;
    }
    state->dashboard_refresh_pending = TRUE;
    g_idle_add(dashboard_refresh_idle, state);
}

void miwear_dashboard_refresh(AppState *state) {
    MiwearDashboardUi *dash = state->dash_ui;
    if (dash == NULL) {
        return;
    }

    GDate *vd = current_view_date(state);
    gchar *day_key = format_day_key(vd);
    GDateTime *pdt = g_date_time_new_local(
        (gint)g_date_get_year(vd),
        (gint)g_date_get_month(vd),
        (gint)g_date_get_day(vd),
        12, 0, 0.0);
    gchar *pretty = g_date_time_format(pdt, "%A, %d %b %Y");
    g_date_time_unref(pdt);
    gtk_label_set_text(GTK_LABEL(dash->date_label), pretty);
    g_free(pretty);
    sync_selected_recorded_day(state, day_key);

    MiwearDayHealthAgg agg;
    miwear_health_aggregate_day(state, day_key, &agg);

    refresh_device_status_chip(state);

    if (state->health_summary_valid && state->health_view_date != NULL) {
        GDateTime *sum_dt = g_date_time_new_from_unix_local((gint64)(state->health_summary_cache.timestamp_ms / 1000LL));
        GDate *sum_d = g_date_new_dmy(
            (GDateDay)g_date_time_get_day_of_month(sum_dt),
            (GDateMonth)g_date_time_get_month(sum_dt),
            (GDateYear)g_date_time_get_year(sum_dt));
        g_date_time_unref(sum_dt);
        if (g_date_compare(sum_d, vd) == 0) {
            if (state->health_summary_cache.steps > 0) {
                agg.steps = state->health_summary_cache.steps;
            }
            if (state->health_summary_cache.calories > 0) {
                agg.calories = state->health_summary_cache.calories;
            }
        }
        g_date_free(sum_d);
    }

    gchar buf[64];
    const gboolean live_tiles = prefer_live_metric_tiles(state);

    if (live_tiles && state->has_live_steps) {
        g_snprintf(buf, sizeof(buf), "%u", state->live_steps);
        card_set_metric(dash->card_steps, buf, "Live", metric_ratio(state->live_steps, GOAL_STEPS));
    } else if (agg.steps >= 0) {
        g_snprintf(buf, sizeof(buf), "%d", agg.steps);
        card_set_metric(dash->card_steps, buf, "Daily steps", metric_ratio(agg.steps, GOAL_STEPS));
    } else {
        card_set_metric(dash->card_steps, "—", "Daily steps", -1.0);
    }
    if (live_tiles && state->has_live_calories) {
        g_snprintf(buf, sizeof(buf), "%u kcal", state->live_calories);
        card_set_metric(dash->card_calories, buf, "Live", metric_ratio(state->live_calories, GOAL_CALORIES));
    } else if (agg.calories >= 0) {
        g_snprintf(buf, sizeof(buf), "%d kcal", agg.calories);
        card_set_metric(dash->card_calories, buf, "Active calories", metric_ratio(agg.calories, GOAL_CALORIES));
    } else {
        card_set_metric(dash->card_calories, "—", "Active calories", -1.0);
    }
    {
        const guint hr_max_goal = state->profile_max_hr > 0 ? state->profile_max_hr : 180u;
        if (live_tiles && state->has_live_hr) {
            g_snprintf(buf, sizeof(buf), "%u bpm", state->live_hr);
            const int zone = miwear_hr_zone_index((int)state->live_hr, (int)state->profile_max_hr);
            const double hr_prog = metric_ratio(state->live_hr, hr_max_goal);
            if (zone > 0) {
                gchar *sub = g_strdup_printf("Live · %s", miwear_hr_zone_label(zone));
                card_set_metric(dash->card_hr, buf, sub, hr_prog);
                g_free(sub);
            } else {
                card_set_metric(dash->card_hr, buf, "Live", hr_prog);
            }
        } else if (agg.hr_n > 0) {
            const int avg_hr = agg.hr_sum / agg.hr_n;
            g_snprintf(buf, sizeof(buf), "%d bpm", avg_hr);
            gchar *sub = g_strdup_printf("Max %d bpm", agg.hr_max);
            card_set_metric(dash->card_hr, buf, sub, metric_ratio(avg_hr, hr_max_goal));
            g_free(sub);
        } else if (state->health_summary_valid) {
            g_snprintf(buf, sizeof(buf), "%d bpm", state->health_summary_cache.hr_avg);
            card_set_metric(dash->card_hr, buf, "Daily average",
                             metric_ratio(state->health_summary_cache.hr_avg, hr_max_goal));
        } else {
            card_set_metric(dash->card_hr, "—", "Heart rate", -1.0);
        }
    }
    if (state->live_spo2_text != NULL && state->live_spo2_text[0] != '\0') {
        const int spo2_live = atoi(state->live_spo2_text);
        card_set_metric(dash->card_spo2, state->live_spo2_text, "SpO₂", metric_ratio(spo2_live, GOAL_SPO2));
    } else if (agg.spo2_n > 0) {
        const int spo2_avg = agg.spo2_sum / agg.spo2_n;
        g_snprintf(buf, sizeof(buf), "%d%%", spo2_avg);
        card_set_metric(dash->card_spo2, buf, "Blood oxygen", metric_ratio(spo2_avg, GOAL_SPO2));
    } else if (state->health_summary_valid &&
               miwear_spo2_value_valid(state->health_summary_cache.spo2_avg)) {
        g_snprintf(buf, sizeof(buf), "%d%% avg", state->health_summary_cache.spo2_avg);
        card_set_metric(dash->card_spo2, buf, "Daily average",
                        metric_ratio(state->health_summary_cache.spo2_avg, GOAL_SPO2));
    } else {
        card_set_metric(dash->card_spo2, "—", "SpO₂", -1.0);
    }
    if (state->live_stress_text != NULL && state->live_stress_text[0] != '\0') {
        const int live_p = atoi(state->live_stress_text);
        gchar *sub = g_strdup_printf("Live · %s", miwear_pressure_level_label(live_p));
        card_set_metric(dash->card_stress, state->live_stress_text, sub,
                        metric_ratio(GOAL_SPO2 - live_p, GOAL_SPO2));
        g_free(sub);
    } else if (agg.stress_n > 0) {
        const int avg_p = agg.stress_sum / agg.stress_n;
        g_snprintf(buf, sizeof(buf), "%d", avg_p);
        gchar *sub = g_strdup_printf("Daily avg · %s", miwear_pressure_level_label(avg_p));
        card_set_metric(dash->card_stress, buf, sub, metric_ratio(GOAL_SPO2 - avg_p, GOAL_SPO2));
        g_free(sub);
    } else if (state->health_summary_valid &&
               miwear_stress_value_valid(state->health_summary_cache.stress_avg)) {
        g_snprintf(buf, sizeof(buf), "%d", state->health_summary_cache.stress_avg);
        gchar *sub = g_strdup_printf("Daily avg · %s",
                                     miwear_pressure_level_label(state->health_summary_cache.stress_avg));
        card_set_metric(dash->card_stress, buf, sub,
                        metric_ratio(GOAL_SPO2 - state->health_summary_cache.stress_avg, GOAL_SPO2));
        g_free(sub);
    } else {
        card_set_metric(dash->card_stress, "—", "Measure on band, then sync", -1.0);
    }

    const int sleep_min = miwear_health_sleep_total_minutes(&agg);
    if (sleep_min > 0) {
        g_snprintf(buf, sizeof(buf), "%dh %dm", sleep_min / 60, sleep_min % 60);
        gchar *sub = NULL;
        if (miwear_health_sleep_has_stage_breakdown(&agg)) {
            sub = g_strdup_printf("Deep %d · Light %d · REM %d · Awake %d",
                                  agg.sleep_deep, agg.sleep_light, agg.sleep_rem, agg.sleep_awake);
        } else {
            sub = g_strdup("Time in bed — stage detail not in database yet");
        }
        card_set_metric(dash->card_sleep, buf, sub, metric_ratio(sleep_min, GOAL_SLEEP_MIN));
        g_free(sub);
        if (miwear_health_sleep_has_stage_breakdown(&agg)) {
            g_snprintf(buf, sizeof(buf),
                       "Deep %d min · Light %d min · REM %d min · Awake %d min",
                       agg.sleep_deep, agg.sleep_light, agg.sleep_rem, agg.sleep_awake);
        } else {
            g_snprintf(buf, sizeof(buf), "%d min total (bed/wake). Re-sync sleep stage files for breakdown.", sleep_min);
        }
        gtk_label_set_text(GTK_LABEL(dash->sleep_stats), buf);
    } else {
        card_set_metric(dash->card_sleep, "—", "Sleep", -1.0);
        gtk_label_set_text(GTK_LABEL(dash->sleep_stats), "No sleep timeline for this day.");
    }

    {
        const guint activity_goal =
            state->goal_moving > 0 ? state->goal_moving : miwear_activity_goal_default();
        guint activity_min = 0;
        gboolean has_activity = FALSE;
        if (state->has_live_moving && state->live_moving <= 180) {
            activity_min = state->live_moving;
            has_activity = TRUE;
        } else if (state->has_activity_minutes_today && state->activity_minutes_today <= 180) {
            activity_min = state->activity_minutes_today;
            has_activity = TRUE;
        }
        if (has_activity) {
            miwear_format_activity_minutes(activity_min, activity_goal, buf, sizeof(buf));
            card_set_metric(dash->card_standing, buf, state->has_live_moving ? "Live" : "From synced minutes",
                            metric_ratio(activity_min, activity_goal));
        } else {
            card_set_metric(dash->card_standing, "—", "Activity time (Moving goal)", -1.0);
        }
    }

    {
        guint last_date = 0;
        const float weight_kg = miwear_weight_log_latest_kg(state, &last_date);
        if (weight_kg > 0.0f) {
            g_snprintf(buf, sizeof(buf), "%.1f kg", weight_kg);
            const float bmi = miwear_profile_bmi_kg_m2(weight_kg, state->profile_height_cm);
            gchar *date_txt = format_yyyymmdd_short(last_date);
            GString *sub = g_string_new(NULL);
            if (bmi > 0.0f) {
                g_string_append_printf(sub, "BMI %.1f", bmi);
            }
            if (state->weight_log != NULL && state->weight_log->len > 1) {
                const MiwearWeightEntry *prev =
                    &g_array_index(state->weight_log, MiwearWeightEntry, 1);
                const float delta = weight_kg - prev->kg;
                if (sub->len > 0) {
                    g_string_append(sub, " · ");
                }
                g_string_append_printf(sub, "%+.1f kg", delta);
            }
            if (date_txt != NULL && date_txt[0] != '\0') {
                if (sub->len > 0) {
                    g_string_append(sub, " · ");
                }
                g_string_append_printf(sub, "%s", date_txt);
            }
            if (sub->len == 0) {
                g_string_append(sub, "Tap to update");
            }
            card_set(dash->card_weight, buf, sub->str);
            g_string_free(sub, TRUE);
            g_free(date_txt);
        } else {
            card_set(dash->card_weight, "Log weight", "Tap to add a reading");
        }
    }

    if (state->has_wellness_bp) {
        g_snprintf(buf, sizeof(buf), "%d/%d",
                   state->wellness_bp_systolic, state->wellness_bp_diastolic);
        card_set(dash->card_bp, buf, "mmHg");
    } else {
        card_set(dash->card_bp, "—", "Blood pressure");
    }

    if (miwear_profile_shows_cycles(state->profile_gender)) {
        gtk_widget_set_visible(dash->card_cycles, TRUE);
        GDate *today = NULL;
        if (state->health_view_date != NULL) {
            today = g_date_copy(state->health_view_date);
        } else {
            GDateTime *now = g_date_time_new_now_local();
            today = g_date_new_dmy(
                (GDateDay)g_date_time_get_day_of_month(now),
                (GDateMonth)g_date_time_get_month(now),
                (GDateYear)g_date_time_get_year(now));
            g_date_time_unref(now);
        }
        const int cycle_day = miwear_cycle_day_of_period(
            today, state->profile_last_period_yyyymmdd, state->profile_cycle_length_days);
        g_date_free(today);
        if (cycle_day > 0) {
            g_snprintf(buf, sizeof(buf), "Day %d", cycle_day);
            card_set(dash->card_cycles, buf,
                     miwear_cycle_phase_label(cycle_day, state->profile_cycle_length_days));
        } else {
            card_set(dash->card_cycles, "—", "Set last_period in config");
        }
    } else {
        gtk_widget_set_visible(dash->card_cycles, FALSE);
    }
    if (state->health_summary_valid && state->health_summary_cache.has_training_vitality) {
        g_snprintf(buf, sizeof(buf), "%d", state->health_summary_cache.vitality_current);
        card_set_metric(dash->card_vitality, buf, "Vitality score",
                        metric_ratio(state->health_summary_cache.vitality_current, GOAL_VITALITY));
    }

    if (dash->weather_status != NULL) {
        const char *wx = state->weather_last_status != NULL ? state->weather_last_status : "Weather not synced yet";
        gtk_label_set_text(GTK_LABEL(dash->weather_status), wx);
    }
    if (dash->find_phone_status != NULL) {
        if (miwear_find_phone_is_active(state)) {
            gtk_label_set_text(GTK_LABEL(dash->find_phone_status), "Find phone: ringing — tap Stop or dismiss on band.");
            if (GTK_IS_WIDGET(dash->btn_find_phone_stop)) {
                gtk_widget_set_sensitive(dash->btn_find_phone_stop, TRUE);
            }
        } else {
            gtk_label_set_text(GTK_LABEL(dash->find_phone_status),
                               "Find phone: band can ring this PC (Mi Fit → find phone).");
            if (GTK_IS_WIDGET(dash->btn_find_phone_stop)) {
                gtk_widget_set_sensitive(dash->btn_find_phone_stop, FALSE);
            }
        }
    }

    g_free(day_key);
    if (dash->sleep_hypnogram != NULL) {
        gtk_widget_queue_draw(dash->sleep_hypnogram);
    }
}

static void on_date_prev(GtkButton *b, gpointer user_data) {
    (void)b;
    AppState *state = user_data;
    GDate *vd = current_view_date(state);
    g_date_subtract_days(vd, 1);
    miwear_dashboard_refresh(state);
}

static void on_date_next(GtkButton *b, gpointer user_data) {
    (void)b;
    AppState *state = user_data;
    GDate *vd = current_view_date(state);
    g_date_add_days(vd, 1);
    miwear_dashboard_refresh(state);
}

static void on_sync_clicked(GtkButton *b, gpointer user_data) {
    (void)b;
    AppState *state = user_data;
    miwear_fitness_sync_resync(state);
}

static void on_find_band_clicked(GtkButton *b, gpointer user_data) {
    (void)b;
    AppState *state = user_data;
    if (miwear_wire_find_band(state, TRUE)) {
        set_status(state, "Find band: vibrating on watch (~45s).");
    } else {
        set_status(state, "Find band failed (connect first).");
    }
}

static void on_find_phone_stop_clicked(GtkButton *b, gpointer user_data) {
    (void)b;
    miwear_find_phone_dismiss(user_data);
}

static void on_weather_clicked(GtkButton *b, gpointer user_data) {
    (void)b;
    AppState *state = user_data;
    miwear_weather_push_cached(state);
}

static void on_notifications_toggled(GObject *obj, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    AppState *state = user_data;
    const gboolean on = gtk_switch_get_active(GTK_SWITCH(obj));
    miwear_notifications_set_enabled(state, on);
}

static void on_notif_test_clicked(GtkButton *b, gpointer user_data) {
    (void)b;
    miwear_notifications_send_test(user_data);
}

void miwear_dashboard_set_notification_status(AppState *state, const char *msg) {
    if (state == NULL || state->dash_ui == NULL) {
        return;
    }
    GtkWidget *label = state->dash_ui->notification_status;
    if (!GTK_IS_LABEL(label)) {
        return;
    }
    gtk_label_set_text(GTK_LABEL(label), msg != NULL ? msg : "");
}

static int hypnogram_stage_band(int stage) {
    switch (stage) {
        case 5: return 0; /* awake — top */
        case 4: return 1; /* REM */
        case 3: return 2; /* light */
        case 2: return 3; /* deep */
        default: return 4; /* unspecified / other */
    }
}

static void hypnogram_set_stage_color(cairo_t *cr, int stage, gboolean dark) {
    switch (stage) {
        case 2:
            cairo_set_source_rgb(cr, dark ? 0.35 : 0.25, dark ? 0.45 : 0.35, dark ? 0.95 : 0.85);
            break;
        case 3:
            cairo_set_source_rgb(cr, dark ? 0.45 : 0.35, dark ? 0.75 : 0.65, dark ? 0.95 : 0.85);
            break;
        case 4:
            cairo_set_source_rgb(cr, dark ? 0.75 : 0.65, dark ? 0.55 : 0.45, dark ? 0.95 : 0.85);
            break;
        case 5:
            cairo_set_source_rgb(cr, dark ? 0.95 : 0.85, dark ? 0.75 : 0.65, dark ? 0.35 : 0.25);
            break;
        case MIWEAR_SLEEP_STAGE_UNSPEC:
            cairo_set_source_rgb(cr, dark ? 0.55 : 0.45, dark ? 0.58 : 0.48, dark ? 0.62 : 0.52);
            break;
        default:
            cairo_set_source_rgb(cr, dark ? 0.40 : 0.30, dark ? 0.40 : 0.30, dark ? 0.45 : 0.35);
            break;
    }
}

static void hypnogram_format_minute(guint minute_of_day, gchar *buf, size_t buf_len) {
    const guint hh = (minute_of_day / 60u) % 24u;
    const guint mm = minute_of_day % 60u;
    g_snprintf(buf, buf_len, "%02u:%02u", hh, mm);
}

static void draw_sleep_hypnogram(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
    (void)area;
    AppState *state = user_data;
    if (width <= 0 || height <= 0 || state == NULL) {
        return;
    }
    const double text_scale =
        (state->dash_ui != NULL && state->dash_ui->ui_scale > 0.0) ? state->dash_ui->ui_scale : 1.0;

    const gboolean dark = adw_style_manager_get_dark(adw_style_manager_get_default());
    const double bg = dark ? 0.11 : 0.96;
    const double plot_bg = dark ? 0.15 : 0.99;
    const double grid = dark ? 0.28 : 0.88;
    const double label = dark ? 0.78 : 0.32;

    cairo_set_source_rgb(cr, bg, bg, bg);
    cairo_paint(cr);

    GDate *vd = current_view_date(state);
    gchar *day_key = format_day_key(vd);

    const double margin_l = 56.0;
    const double margin_r = 12.0;
    const double margin_t = 10.0;
    const double margin_b = 26.0;
    const double plot_left = margin_l;
    const double plot_top = margin_t;
    const double plot_w = (double)width - margin_l - margin_r;
    const double plot_h = (double)height - margin_t - margin_b;

    static const char *band_labels[] = {"Awake", "REM", "Light", "Deep", "Sleep"};
    const int num_bands = 5;
    const double band_h = plot_h / (double)num_bands;

    g_mutex_lock(&state->recorded_lock);
    GArray *arr = (state->recorded_days != NULL) ? g_hash_table_lookup(state->recorded_days, day_key) : NULL;

    if (arr == NULL || arr->len == 0) {
        g_mutex_unlock(&state->recorded_lock);
        g_free(day_key);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 12.0 * text_scale);
        cairo_set_source_rgb(cr, label, label, label + 0.02);
        cairo_move_to(cr, plot_left, plot_top + plot_h * 0.5);
        cairo_show_text(cr, "No sleep data");
        return;
    }

    guint first = G_MAXUINT, last = 0;
    for (guint i = 0; i < arr->len; i++) {
        const RecordedSample *s = &g_array_index(arr, RecordedSample, i);
        if (s->sleep_stage < 0) {
            continue;
        }
        if (i < first) {
            first = i;
        }
        if (i > last) {
            last = i;
        }
    }

    if (first == G_MAXUINT || last <= first) {
        g_mutex_unlock(&state->recorded_lock);
        g_free(day_key);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 12.0 * text_scale);
        cairo_set_source_rgb(cr, label, label, label + 0.02);
        cairo_move_to(cr, plot_left, plot_top + plot_h * 0.5);
        cairo_show_text(cr, "No sleep timeline for this day");
        return;
    }

    cairo_set_source_rgb(cr, plot_bg, plot_bg, plot_bg);
    cairo_rectangle(cr, plot_left, plot_top, plot_w, plot_h);
    cairo_fill(cr);

    cairo_set_source_rgb(cr, grid, grid, grid);
    cairo_set_line_width(cr, 1.0);
    for (int b = 0; b <= num_bands; b++) {
        const double y = plot_top + (double)b * band_h;
        cairo_move_to(cr, plot_left, y);
        cairo_line_to(cr, plot_left + plot_w, y);
        cairo_stroke(cr);
    }

    const double span = (double)(last - first + 1);
    int prev_stage = -1;
    guint seg_start = first;
    for (guint i = first; i <= last + 1; i++) {
        int stage = -1;
        if (i <= last) {
            const RecordedSample *s = &g_array_index(arr, RecordedSample, i);
            stage = s->sleep_stage;
        }
        if (i > last || stage != prev_stage) {
            if (prev_stage >= 0 && i > seg_start) {
                const double x0 = plot_left + ((double)(seg_start - first) / span) * plot_w;
                const double x1 = plot_left + ((double)(i - first) / span) * plot_w;
                const int band = hypnogram_stage_band(prev_stage);
                const double y0 = plot_top + (double)band * band_h + 1.0;
                const double seg_h = band_h - 2.0;
                hypnogram_set_stage_color(cr, prev_stage, dark);
                cairo_rectangle(cr, x0, y0, x1 - x0, seg_h);
                cairo_fill(cr);
            }
            seg_start = i;
            prev_stage = stage;
        }
    }
    g_mutex_unlock(&state->recorded_lock);
    g_free(day_key);

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 10.0 * text_scale);
    cairo_set_source_rgb(cr, label, label, label + 0.02);
    for (int b = 0; b < num_bands; b++) {
        const double y = plot_top + (double)b * band_h + band_h * 0.65;
        cairo_move_to(cr, 4.0, y);
        cairo_show_text(cr, band_labels[b]);
    }

    gchar t0[16], t1[16];
    hypnogram_format_minute(first, t0, sizeof(t0));
    hypnogram_format_minute(last + 1, t1, sizeof(t1));
    cairo_move_to(cr, plot_left, plot_top + plot_h + 18.0);
    cairo_show_text(cr, t0);
    gchar end_label[24];
    g_snprintf(end_label, sizeof(end_label), "%s", t1);
    cairo_text_extents_t ext;
    cairo_text_extents(cr, end_label, &ext);
    cairo_move_to(cr, plot_left + plot_w - ext.width, plot_top + plot_h + 18.0);
    cairo_show_text(cr, end_label);
}

void miwear_dashboard_update_live(AppState *state,
                                  gboolean has_hr, guint hr,
                                  gboolean has_steps, guint steps,
                                  gboolean has_calories, guint calories,
                                  gboolean has_standing, guint standing,
                                  gboolean has_battery, guint battery,
                                  gboolean has_moving, guint moving,
                                  const gchar *spo2_text,
                                  const gchar *stress_text) {
    MiwearDashboardUi *dash = state->dash_ui;
    if (dash == NULL) {
        return;
    }
    gchar buf[64];
    if (has_battery) {
        g_snprintf(buf, sizeof(buf), "Battery %u%%", battery);
        gtk_label_set_text(GTK_LABEL(dash->battery_chip), buf);
    }
    const guint hr_max_goal = state->profile_max_hr > 0 ? state->profile_max_hr : 180u;
    if (has_hr) {
        g_snprintf(buf, sizeof(buf), "%u bpm", hr);
        const int zone = miwear_hr_zone_index((int)hr, (int)state->profile_max_hr);
        if (zone > 0) {
            gchar *sub = g_strdup_printf("Live · %s", miwear_hr_zone_label(zone));
            card_set_metric(dash->card_hr, buf, sub, metric_ratio(hr, hr_max_goal));
            g_free(sub);
        } else {
            card_set_metric(dash->card_hr, buf, "Live", metric_ratio(hr, hr_max_goal));
        }
    }
    if (has_steps) {
        g_snprintf(buf, sizeof(buf), "%u", steps);
        card_set_metric(dash->card_steps, buf, "Live", metric_ratio(steps, GOAL_STEPS));
    }
    if (has_calories) {
        g_snprintf(buf, sizeof(buf), "%u kcal", calories);
        card_set_metric(dash->card_calories, buf, "Live", metric_ratio(calories, GOAL_CALORIES));
    }
    if (has_moving && moving <= 180) {
        const guint activity_goal =
            state->goal_moving > 0 ? state->goal_moving : miwear_activity_goal_default();
        miwear_format_activity_minutes(moving, activity_goal, buf, sizeof(buf));
        card_set_metric(dash->card_standing, buf, "Live", metric_ratio(moving, activity_goal));
    } else if (has_standing) {
        miwear_format_standing_value(standing, buf, sizeof(buf));
        card_set(dash->card_standing, buf, "Standing hours");
    }
    if (spo2_text != NULL) {
        const int spo2_live = atoi(spo2_text);
        card_set_metric(dash->card_spo2, spo2_text, "SpO₂", metric_ratio(spo2_live, GOAL_SPO2));
    }
    if (stress_text != NULL) {
        const int live_p = atoi(stress_text);
        gchar *sub = g_strdup_printf("Live · %s", miwear_pressure_level_label(live_p));
        card_set_metric(dash->card_stress, stress_text, sub, metric_ratio(GOAL_SPO2 - live_p, GOAL_SPO2));
        g_free(sub);
    }
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

void miwear_dashboard_update_device_state(AppState *state,
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
    refresh_device_status_chip(state);
}

void miwear_dashboard_notify_summary(AppState *state, const MiwearDailySummaryRow *row) {
    if (state == NULL || row == NULL) {
        return;
    }
    state->health_summary_cache = *row;
    state->health_summary_valid = TRUE;
    miwear_dashboard_schedule_refresh(state);
}

static gchar *format_workout_line(const MiwearSportEntry *e) {
    GString *s = g_string_new(e->label);
    if (e->duration_sec > 0) {
        g_string_append_printf(s, " · %d min", (e->duration_sec + 59) / 60);
    }
    if (e->distance_m > 0) {
        if (e->distance_m >= 1000) {
            g_string_append_printf(s, " · %.2f km", e->distance_m / 1000.0);
        } else {
            g_string_append_printf(s, " · %d m", e->distance_m);
        }
    }
    if (e->calories > 0) {
        g_string_append_printf(s, " · %d kcal", e->calories);
    }
    if (e->hr_avg > 0) {
        g_string_append_printf(s, " · HR %d", e->hr_avg);
    }
    return g_string_free(s, FALSE);
}

void miwear_sport_add_entry(AppState *state,
                            uint32_t ts,
                            int type,
                            int subtype,
                            const MiwearWorkoutSummary *summary) {
    MiwearSportEntry *e = g_new0(MiwearSportEntry, 1);
    e->ts_sec = ts;
    e->sport_type = type;
    e->sport_subtype = subtype;
    if (summary != NULL) {
        e->start_sec = summary->start_sec;
        e->end_sec = summary->end_sec;
        e->duration_sec = summary->duration_sec;
        e->distance_m = summary->distance_m;
        e->calories = summary->calories;
        e->hr_avg = summary->hr_avg;
        e->label = g_strdup(summary->sport_name != NULL ? summary->sport_name
                                                          : miwear_sport_subtype_name(subtype));
    } else {
        e->label = g_strdup_printf("%s", activity_subtype_name(type, subtype));
    }
    state->sport_entries = g_list_prepend(state->sport_entries, e);

    MiwearDashboardUi *dash = state->dash_ui;
    if (dash != NULL && dash->sport_list != NULL) {
        const uint32_t show_ts = e->start_sec > 0 ? e->start_sec : ts;
        GDateTime *dt = g_date_time_new_from_unix_local((gint64)show_ts);
        gchar *when = g_date_time_format(dt, "%Y-%m-%d %H:%M");
        g_date_time_unref(dt);
        gchar *detail = format_workout_line(e);
        gchar *txt = g_strdup_printf("%s — %s", when, detail);
        g_free(when);
        g_free(detail);
        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *lbl = gtk_label_new(txt);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
        gtk_label_set_wrap(GTK_LABEL(lbl), TRUE);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), lbl);
        gtk_list_box_insert(GTK_LIST_BOX(dash->sport_list), row, 0);
        g_free(txt);
    }
}

static GtkWidget *build_health_tab(AppState *state, MiwearDashboardUi *dash) {
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(v, 12);
    gtk_widget_set_margin_bottom(v, 12);
    gtk_widget_set_margin_start(v, 12);
    gtk_widget_set_margin_end(v, 12);

    GtkWidget *head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *prev = gtk_button_new_with_label("‹");
    GtkWidget *next = gtk_button_new_with_label("›");
    dash->date_label = gtk_label_new("Today");
    gtk_widget_add_css_class(dash->date_label, "mi-header");
    gtk_widget_set_hexpand(dash->date_label, TRUE);
    gtk_label_set_xalign(GTK_LABEL(dash->date_label), 0.5f);
    g_signal_connect(prev, "clicked", G_CALLBACK(on_date_prev), state);
    g_signal_connect(next, "clicked", G_CALLBACK(on_date_next), state);
    gtk_box_append(GTK_BOX(head), prev);
    gtk_box_append(GTK_BOX(head), dash->date_label);
    gtk_box_append(GTK_BOX(head), next);

    dash->status_chip = gtk_label_new("Device: —");
    gtk_widget_add_css_class(dash->status_chip, "mi-chip");
    dash->battery_chip = gtk_label_new("Battery —");
    gtk_widget_add_css_class(dash->battery_chip, "mi-chip");

    GtkWidget *chips = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(chips), dash->status_chip);
    gtk_box_append(GTK_BOX(chips), dash->battery_chip);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);

    dash->card_steps = make_gauge_card("Steps", "—", "Daily steps");
    dash->card_calories = make_gauge_card("Calories", "—", "Active calories");
    dash->card_hr = make_gauge_card("Heart rate", "—", "");
    dash->card_spo2 = make_gauge_card("SpO₂", "—", "");
    dash->card_stress = make_gauge_card("Pressure", "—", "Stress score 1–100");
    dash->card_sleep = make_gauge_card("Sleep", "—", "Last night");
    dash->card_standing = make_gauge_card("Activity time", "—", "min / goal");
    dash->card_vitality = make_gauge_card("Vitality", "—", "Training load");
    dash->card_energy = make_gauge_card("Energy", "—", "Body battery");
    dash->card_weight = make_gauge_card("Weight", "Log weight", "Tap to add a reading");
    gtk_widget_set_tooltip_text(dash->card_weight, "Manual weight log (not synced from the band)");
    {
        GtkEventController *click = GTK_EVENT_CONTROLLER(gtk_gesture_click_new());
        g_signal_connect(click, "pressed", G_CALLBACK(on_weight_card_pressed), state);
        gtk_widget_add_controller(dash->card_weight, click);
    }
    dash->card_bp = make_gauge_card("Blood pressure", "—", "Cuff BP (not on band 9/10)");
    gtk_widget_set_visible(dash->card_bp, FALSE);
    dash->card_cycles = make_gauge_card("Cycles", "—", "Menstrual cycle");
    gtk_widget_set_visible(dash->card_cycles, miwear_profile_shows_cycles(state->profile_gender));

    gtk_grid_attach(GTK_GRID(grid), dash->card_steps, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), dash->card_calories, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), dash->card_hr, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), dash->card_spo2, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), dash->card_stress, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), dash->card_sleep, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), dash->card_standing, 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), dash->card_vitality, 1, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), dash->card_weight, 0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), dash->card_bp, 1, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), dash->card_cycles, 0, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), dash->card_energy, 1, 5, 1, 1);

    gtk_box_append(GTK_BOX(v), head);
    gtk_box_append(GTK_BOX(v), chips);
    gtk_box_append(GTK_BOX(v), grid);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), v);
    return scroll;
}

static GtkWidget *build_sleep_tab(AppState *state, MiwearDashboardUi *dash) {
    GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(v, 12);
    gtk_widget_set_margin_start(v, 12);
    gtk_widget_set_margin_end(v, 12);
    dash->sleep_hypnogram = gtk_drawing_area_new();
    gtk_widget_set_size_request(dash->sleep_hypnogram, -1, 220);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(dash->sleep_hypnogram), draw_sleep_hypnogram, state, NULL);
    dash->sleep_stats = gtk_label_new("Sleep stage breakdown");
    gtk_label_set_xalign(GTK_LABEL(dash->sleep_stats), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(dash->sleep_stats), TRUE);
    gtk_box_append(GTK_BOX(v), dash->sleep_hypnogram);
    gtk_box_append(GTK_BOX(v), dash->sleep_stats);
    return v;
}

static GtkWidget *build_sport_tab(MiwearDashboardUi *dash) {
    GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(v, 12);
    gtk_widget_set_margin_start(v, 12);
    gtk_widget_set_margin_end(v, 12);
    GtkWidget *hint = gtk_label_new("Workouts synced from the band (Mi Fitness sport files).");
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(hint), TRUE);
    dash->sport_list = gtk_list_box_new();
    gtk_widget_set_vexpand(dash->sport_list, TRUE);
    gtk_box_append(GTK_BOX(v), hint);
    gtk_box_append(GTK_BOX(v), dash->sport_list);
    return v;
}

static GtkWidget *build_device_tab(AppState *state, MiwearDashboardUi *dash) {
    GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(v, 12);
    gtk_widget_set_margin_start(v, 12);
    gtk_widget_set_margin_end(v, 12);
    dash->btn_sync = gtk_button_new_with_label("Sync health data");
    g_signal_connect(dash->btn_sync, "clicked", G_CALLBACK(on_sync_clicked), state);
    state->btn_refresh = dash->btn_sync;
    dash->btn_weather = gtk_button_new_with_label("Push weather to band");
    g_signal_connect(dash->btn_weather, "clicked", G_CALLBACK(on_weather_clicked), state);
    dash->btn_find_band = gtk_button_new_with_label("Find band (vibrate watch)");
    g_signal_connect(dash->btn_find_band, "clicked", G_CALLBACK(on_find_band_clicked), state);
    dash->btn_find_phone_stop = gtk_button_new_with_label("Stop find phone");
    gtk_widget_set_sensitive(dash->btn_find_phone_stop, FALSE);
    g_signal_connect(dash->btn_find_phone_stop, "clicked", G_CALLBACK(on_find_phone_stop_clicked), state);
    dash->find_phone_status = gtk_label_new("Find phone: idle");
    gtk_label_set_xalign(GTK_LABEL(dash->find_phone_status), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(dash->find_phone_status), TRUE);
    dash->weather_status = gtk_label_new("Weather not synced yet");
    gtk_label_set_xalign(GTK_LABEL(dash->weather_status), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(dash->weather_status), TRUE);

    GtkWidget *notif_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *notif_label = gtk_label_new("Forward desktop notifications");
    gtk_label_set_xalign(GTK_LABEL(notif_label), 0.0f);
    gtk_widget_set_hexpand(notif_label, TRUE);
    dash->switch_notifications = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(dash->switch_notifications), state->notifications_enabled);
    g_signal_connect(dash->switch_notifications, "notify::active", G_CALLBACK(on_notifications_toggled), state);
    gtk_box_append(GTK_BOX(notif_row), notif_label);
    gtk_box_append(GTK_BOX(notif_row), dash->switch_notifications);

    dash->btn_notif_test = gtk_button_new_with_label("Send test notification to band");
    g_signal_connect(dash->btn_notif_test, "clicked", G_CALLBACK(on_notif_test_clicked), state);
    dash->notification_status = gtk_label_new(
        state->notification_status != NULL ? state->notification_status : "Notifications idle");
    gtk_label_set_xalign(GTK_LABEL(dash->notification_status), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(dash->notification_status), TRUE);
    GtkWidget *tv = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(tv), TRUE);
    state->log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv));
    dash->device_log_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(dash->device_log_scroll), tv);
    gtk_widget_set_vexpand(dash->device_log_scroll, TRUE);
    gtk_box_append(GTK_BOX(v), dash->btn_sync);
    gtk_box_append(GTK_BOX(v), dash->btn_weather);
    gtk_box_append(GTK_BOX(v), dash->btn_find_band);
    gtk_box_append(GTK_BOX(v), dash->btn_find_phone_stop);
    gtk_box_append(GTK_BOX(v), dash->find_phone_status);
    gtk_box_append(GTK_BOX(v), dash->weather_status);
    gtk_box_append(GTK_BOX(v), notif_row);
    gtk_box_append(GTK_BOX(v), dash->btn_notif_test);
    gtk_box_append(GTK_BOX(v), dash->notification_status);
    gtk_box_append(GTK_BOX(v), dash->device_log_scroll);
    return v;
}

GtkWidget *miwear_dashboard_build(AppState *state, MiwearDashboardUi *dash) {
    state->dash_ui = dash;

    GtkStack *stack = GTK_STACK(gtk_stack_new());
    gtk_stack_add_named(stack, build_health_tab(state, dash), "health");
    gtk_stack_add_named(stack, build_sleep_tab(state, dash), "sleep");
    gtk_stack_add_named(stack, build_sport_tab(dash), "sport");
    gtk_stack_add_named(stack, build_device_tab(state, dash), "device");
    gtk_stack_set_visible_child_name(stack, "health");
    gtk_stack_set_transition_type(stack, GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(stack, 120);

    GtkWidget *nav = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_add_css_class(nav, "mi-bottom-nav");
    GtkWidget *health_btn = make_bottom_nav_button(dash, stack, NULL, "health", "Health", "tab-health.svg");
    GtkToggleButton *group = GTK_TOGGLE_BUTTON(health_btn);
    gtk_toggle_button_set_active(group, TRUE);
    gtk_box_append(GTK_BOX(nav), health_btn);
    gtk_box_append(GTK_BOX(nav), make_bottom_nav_button(dash, stack, group, "sleep", "Sleep", "tab-sleep.svg"));
    gtk_box_append(GTK_BOX(nav), make_bottom_nav_button(dash, stack, group, "sport", "Workouts", "tab-sport.svg"));
    gtk_box_append(GTK_BOX(nav), make_bottom_nav_button(dash, stack, group, "device", "Device", "tab-device.svg"));

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(root), GTK_WIDGET(stack));
    gtk_widget_set_vexpand(GTK_WIDGET(stack), TRUE);
    gtk_box_append(GTK_BOX(root), nav);

    state->label_status = gtk_label_new("Idle");
    gtk_widget_add_css_class(state->label_status, "mi-status-footer");
    gtk_label_set_xalign(GTK_LABEL(state->label_status), 0.0f);
    gtk_widget_set_margin_start(state->label_status, 12);
    gtk_widget_set_margin_end(state->label_status, 12);
    gtk_widget_set_margin_bottom(state->label_status, 8);
    gtk_box_append(GTK_BOX(root), state->label_status);
    install_dashboard_css(root, dash);

    /* Hidden day key list for DB rebuild / merge (no visible dropdown). */
    GtkStringList *day_strings = gtk_string_list_new(NULL);
    state->recorded_day_dropdown = gtk_drop_down_new(G_LIST_MODEL(day_strings), NULL);
    g_object_unref(day_strings);
    gtk_widget_set_visible(state->recorded_day_dropdown, FALSE);

    return root;
}
