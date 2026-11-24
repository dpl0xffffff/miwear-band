#include "ui_private.h"

#include <math.h>
#include <string.h>

#define SLEEP_GOAL_MIN 480

typedef struct {
    const char *name;
    UiColor color;
    int stage;
} StageSpec;

/* Top to bottom in the hypnogram. */
static const StageSpec stage_specs[4] = {
    {"Awake", UI_COLOR_AWAKE, 5},
    {"REM", UI_COLOR_REM, 4},
    {"Light", UI_COLOR_LIGHT, 3},
    {"Deep", UI_COLOR_DEEP, 2},
};

static int stage_lane(int stage) {
    switch (stage) {
        case 5: return 0;
        case 4: return 1;
        case 2: return 3;
        default: return 2; /* light and "asleep, stage unknown" */
    }
}

static const char *stage_name(int stage) {
    switch (stage) {
        case 5: return "Awake";
        case 4: return "REM";
        case 3: return "Light";
        case 2: return "Deep";
        default: return "Asleep";
    }
}

/* ---- Stage bar ---------------------------------------------------------------------------- */

static void draw_stage_bar(GtkDrawingArea *area, cairo_t *cr, int w, int h, gpointer user_data) {
    (void)area;
    SalusDashboardUi *dash = user_data;
    const UiNight *n = dash->night;
    const int parts[4] = {n->awake, n->rem, n->light + (n->has_breakdown ? 0 : n->other), n->deep};
    const int total = parts[0] + parts[1] + parts[2] + parts[3];
    if (total <= 0) {
        return;
    }
    cairo_save(cr);
    ui_rounded_rect(cr, 0, 0, w, h, h / 2.0);
    cairo_clip(cr);
    /* Deep → light → REM → awake from left to right. */
    double x = 0;
    for (int i = 3; i >= 0; i--) {
        if (parts[i] <= 0) {
            continue;
        }
        const double pw = (double)w * parts[i] / total;
        ui_cairo_set_color(cr, stage_specs[i].color, 1.0);
        cairo_rectangle(cr, x, 0, pw + 0.5, h);
        cairo_fill(cr);
        x += pw;
        if (x < w - 1) {
            cairo_set_source_rgba(cr, 0, 0, 0, 0);
        }
    }
    cairo_restore(cr);
}

static GtkWidget *legend_item(UiColor color, const char *name, GtkWidget **value_out) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *dot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(dot, "salus-dot");
    gtk_widget_add_css_class(dot, ui_color_css_class(color));
    gtk_widget_set_valign(dot, GTK_ALIGN_CENTER);
    GtkWidget *col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(col), ui_label(name, "caption dimmed", 0.0f));
    GtkWidget *value = ui_label("—", "salus-stat-value numeric", 0.0f);
    gtk_label_set_use_markup(GTK_LABEL(value), TRUE);
    gtk_box_append(GTK_BOX(col), value);
    gtk_box_append(GTK_BOX(row), dot);
    gtk_box_append(GTK_BOX(row), col);
    *value_out = value;
    return row;
}

/* ---- Hypnogram ---------------------------------------------------------------------------- */

static void hypno_bounds(const UiNight *n, int *from, int *to) {
    *from = MAX(0, n->first - 20);
    *to = MIN(UI_NIGHT_MINUTES, n->last + 21);
    /* Round to whole hours of the clock for tidy axis labels. */
    *from = ((UI_NIGHT_START_MIN + *from) / 60) * 60 - UI_NIGHT_START_MIN;
    *from = MAX(0, *from);
    *to = ((UI_NIGHT_START_MIN + *to + 59) / 60) * 60 - UI_NIGHT_START_MIN;
    *to = MIN(UI_NIGHT_MINUTES, *to);
}

static void draw_hypnogram(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
    SalusDashboardUi *dash = user_data;
    GtkWidget *w = GTK_WIDGET(area);
    const UiNight *n = dash->night;
    if (n->first < 0 || width < 80) {
        return;
    }
    int from, to;
    hypno_bounds(n, &from, &to);
    const int span = MAX(to - from, 1);
    const double left = 52, right = 8, top = 4, bottom = 22;
    const double pw = width - left - right, ph = height - top - bottom;
    const double lane_h = ph / 4.0;
#define HX(i) (left + pw * (double)((i) - from) / (double)span)

    for (int l = 0; l < 4; l++) {
        const double y = top + l * lane_h;
        ui_draw_text(cr, w, stage_specs[l].name, 0, y + lane_h / 2 + 4, 11, 0.6, 0.0);
        ui_cairo_set_fg(cr, w, 0.06);
        ui_rounded_rect(cr, left, y + 2, pw, lane_h - 4, 6);
        cairo_fill(cr);
    }
    /* One label per N hours so that labels stay ~48 px apart at any width. */
    const double px_per_hour = pw * 60.0 / span;
    const int step = MAX(1, (int)ceil(48.0 / MAX(px_per_hour, 1.0)));
    for (int m = from; m <= to; m += 60 * step) {
        gchar t[8];
        ui_format_clock(UI_NIGHT_START_MIN + m, t, sizeof(t));
        ui_draw_text(cr, w, t, CLAMP(HX(m), left + 14, left + pw - 14), top + ph + 16, 10.5, 0.55, 0.5);
    }

    /* Transitions first (thin connectors), blocks on top. */
    int prev_lane = -1;
    double prev_x = 0;
    int i = n->first;
    while (i <= n->last) {
        const int st = n->stage[i];
        int j = i;
        while (j + 1 <= n->last && n->stage[j + 1] == st) {
            j++;
        }
        if (st >= 0) {
            const int lane = stage_lane(st);
            const double x0 = HX(i), x1 = HX(j + 1);
            if (prev_lane >= 0 && prev_lane != lane && fabs(prev_x - x0) < 2.0) {
                ui_cairo_set_fg(cr, w, 0.18);
                cairo_set_line_width(cr, 1.0);
                const double ya = top + prev_lane * lane_h + lane_h / 2, yb = top + lane * lane_h + lane_h / 2;
                cairo_move_to(cr, round(x0) + 0.5, ya);
                cairo_line_to(cr, round(x0) + 0.5, yb);
                cairo_stroke(cr);
            }
            if (st == 2 || st == 3 || st == 4 || st == 5) {
                ui_cairo_set_color(cr, stage_specs[lane].color, 1.0);
                ui_rounded_rect(cr, x0, top + lane * lane_h + lane_h * 0.18, MAX(x1 - x0, 1.5), lane_h * 0.64, 4);
            } else {
                /* Asleep, stage unknown: span every lane so it doesn't read as light sleep. */
                ui_cairo_set_fg(cr, w, 0.16);
                ui_rounded_rect(cr, x0, top + 4, MAX(x1 - x0, 1.5), ph - 8, 6);
            }
            cairo_fill(cr);
            prev_lane = lane;
            prev_x = x1;
        }
        i = j + 1;
    }

    if (dash->sleep_hover_x >= left && dash->sleep_hover_x <= left + pw) {
        const int at = CLAMP(from + (int)((dash->sleep_hover_x - left) / pw * span), 0, UI_NIGHT_MINUTES - 1);
        if (n->stage[at] >= 0) {
            ui_cairo_set_fg(cr, w, 0.35);
            cairo_move_to(cr, round(HX(at)) + 0.5, top);
            cairo_line_to(cr, round(HX(at)) + 0.5, top + ph);
            cairo_stroke(cr);
            gchar c[8], tip[48];
            ui_format_clock(UI_NIGHT_START_MIN + at, c, sizeof(c));
            g_snprintf(tip, sizeof(tip), "%s   %s", c, stage_name(n->stage[at]));
            ui_draw_tooltip(cr, w, tip, HX(at), top, left + pw);
        }
    }
#undef HX
}

static void on_hypno_motion(GtkEventControllerMotion *m, double x, double y, gpointer user_data) {
    (void)m;
    (void)y;
    SalusDashboardUi *dash = user_data;
    dash->sleep_hover_x = x;
    gtk_widget_queue_draw(dash->sleep_hypnogram);
}

static void on_hypno_leave(GtkEventControllerMotion *m, gpointer user_data) {
    (void)m;
    SalusDashboardUi *dash = user_data;
    dash->sleep_hover_x = -1;
    gtk_widget_queue_draw(dash->sleep_hypnogram);
}

/* ---- Last 7 nights ------------------------------------------------------------------------ */

#define WEEK_LEFT 34.0
#define WEEK_BOTTOM 24.0

static void draw_week(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
    SalusApp *state = user_data;
    SalusDashboardUi *dash = state->dash_ui;
    GtkWidget *w = GTK_WIDGET(area);
    const double left = WEEK_LEFT, top = 6, bottom = WEEK_BOTTOM;
    const double pw = width - left, ph = height - top - bottom;
    int max_min = SLEEP_GOAL_MIN + 60;
    for (int i = 0; i < 7; i++) {
        max_min = MAX(max_min, dash->week_minutes[i]);
    }
    const int hi_h = (max_min + 59) / 60;
    for (int hh = 0; hh <= hi_h; hh += (hi_h > 10 ? 4 : 2)) {
        const double y = top + ph * (1.0 - (double)hh * 60 / (hi_h * 60));
        ui_cairo_set_fg(cr, w, hh == 0 ? 0.18 : 0.07);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, left, round(y) + 0.5);
        cairo_line_to(cr, left + pw, round(y) + 0.5);
        cairo_stroke(cr);
        gchar t[8];
        g_snprintf(t, sizeof(t), "%dh", hh);
        ui_draw_text(cr, w, t, left - 6, y + 3.5, 10.5, 0.55, 1.0);
    }
    /* Goal line */
    const double gy = top + ph * (1.0 - (double)SLEEP_GOAL_MIN / (hi_h * 60));
    const double dashes[] = {4, 4};
    cairo_set_dash(cr, dashes, 2, 0);
    ui_cairo_set_color(cr, UI_COLOR_SLEEP, 0.6);
    cairo_move_to(cr, left, round(gy) + 0.5);
    cairo_line_to(cr, left + pw, round(gy) + 0.5);
    cairo_stroke(cr);
    cairo_set_dash(cr, NULL, 0, 0);

    const double slot = pw / 7.0;
    const double bw = MIN(slot * 0.56, 34);
    GDate d = *ui_view_date(state);
    g_date_subtract_days(&d, 6);
    for (int i = 0; i < 7; i++) {
        const double cx = left + slot * (i + 0.5);
        const int mins = dash->week_minutes[i];
        if (mins > 0) {
            const double bh = ph * (double)mins / (hi_h * 60);
            ui_cairo_set_color(cr, UI_COLOR_SLEEP, i == 6 ? 1.0 : 0.45);
            ui_rounded_rect(cr, cx - bw / 2, top + ph - bh, bw, bh, MIN(8, bw / 2));
            cairo_fill(cr);
        }
        static const char *wd[] = {"", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
        ui_draw_text(cr, w, wd[g_date_get_weekday(&d)], cx, top + ph + 17, 10.5, i == 6 ? 0.9 : 0.55, 0.5);
        g_date_add_days(&d, 1);
    }
}

static void on_week_click(GtkGestureClick *g, int n_press, double x, double y, gpointer user_data) {
    (void)n_press;
    (void)y;
    SalusApp *state = user_data;
    GtkWidget *w = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(g));
    const double pw = gtk_widget_get_width(w) - WEEK_LEFT;
    if (x < WEEK_LEFT || pw <= 0) {
        return;
    }
    const int i = CLAMP((int)((x - WEEK_LEFT) / (pw / 7.0)), 0, 6);
    if (i == 6) {
        return;
    }
    GDate d = *ui_view_date(state);
    g_date_subtract_days(&d, 6 - i);
    ui_set_view_date(state, &d);
}

/* ---- Page --------------------------------------------------------------------------------- */

static GtkWidget *legend_values[4];
static GtkWidget *sleep_bed_value, *sleep_wake_value, *sleep_inbed_value;
static GtkWidget *sleep_hr_value, *sleep_spo2_value;
static GtkWidget *sleep_summary_sub;
static GtkWidget *sleep_stages_sub;

GtkWidget *ui_sleep_page_new(SalusApp *state, SalusDashboardUi *dash) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(box, "salus-page");
    gtk_box_append(GTK_BOX(box), ui_date_nav_new(state, dash));

    /* Empty state */
    dash->sleep_empty = adw_status_page_new();
    adw_status_page_set_icon_name(ADW_STATUS_PAGE(dash->sleep_empty), "salus-moon-symbolic");
    adw_status_page_set_title(ADW_STATUS_PAGE(dash->sleep_empty), "No Sleep Recorded");
    adw_status_page_set_description(ADW_STATUS_PAGE(dash->sleep_empty),
                                    "Wear the band overnight and sync in the morning to see your sleep stages.");
    gtk_widget_add_css_class(dash->sleep_empty, "compact");
    gtk_box_append(GTK_BOX(box), dash->sleep_empty);

    dash->sleep_content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);

    /* Summary */
    GtkWidget *inner = NULL;
    GtkWidget *card = ui_card_new(&inner, 14);
    GtkWidget *hero = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *icon = ui_icon("salus-moon-symbolic", UI_COLOR_SLEEP, 22);
    gtk_widget_add_css_class(icon, "salus-bubble");
    gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
    GtkWidget *hero_text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    dash->sleep_total = ui_label("—", "salus-hero-value numeric", 0.0f);
    sleep_summary_sub = ui_label("Asleep", "dimmed", 0.0f);
    gtk_box_append(GTK_BOX(hero_text), dash->sleep_total);
    gtk_box_append(GTK_BOX(hero_text), sleep_summary_sub);
    gtk_box_append(GTK_BOX(hero), icon);
    gtk_box_append(GTK_BOX(hero), hero_text);
    gtk_box_append(GTK_BOX(inner), hero);

    GtkWidget *times = ui_stat_flow_new();
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(times), 3);
    ui_stat_flow_append(times, ui_stat_new("Fell asleep", &sleep_bed_value));
    ui_stat_flow_append(times, ui_stat_new("Woke up", &sleep_wake_value));
    ui_stat_flow_append(times, ui_stat_new("Time in bed", &sleep_inbed_value));
    gtk_box_append(GTK_BOX(inner), times);

    GtkWidget *vitals = ui_stat_flow_new();
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(vitals), 3);
    ui_stat_flow_append(vitals, ui_stat_new("Avg heart rate", &sleep_hr_value));
    ui_stat_flow_append(vitals, ui_stat_new("Avg SpO₂", &sleep_spo2_value));
    gtk_box_append(GTK_BOX(inner), vitals);

    dash->sleep_bar = gtk_drawing_area_new();
    gtk_widget_set_size_request(dash->sleep_bar, -1, 14);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(dash->sleep_bar), draw_stage_bar, dash, NULL);
    gtk_box_append(GTK_BOX(inner), dash->sleep_bar);

    dash->sleep_legend = ui_stat_flow_new();
    for (int i = 3; i >= 0; i--) {
        ui_stat_flow_append(dash->sleep_legend,
                            legend_item(stage_specs[i].color, stage_specs[i].name, &legend_values[i]));
    }
    gtk_box_append(GTK_BOX(inner), dash->sleep_legend);
    gtk_box_append(GTK_BOX(dash->sleep_content), card);

    /* Hypnogram */
    card = ui_card_new(&inner, 12);
    gtk_box_append(GTK_BOX(inner), ui_card_header("Sleep Stages", &sleep_stages_sub, NULL));
    dash->sleep_hypnogram = gtk_drawing_area_new();
    gtk_widget_set_size_request(dash->sleep_hypnogram, -1, 190);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(dash->sleep_hypnogram), draw_hypnogram, dash, NULL);
    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_hypno_motion), dash);
    g_signal_connect(motion, "leave", G_CALLBACK(on_hypno_leave), dash);
    gtk_widget_add_controller(dash->sleep_hypnogram, motion);
    gtk_box_append(GTK_BOX(inner), dash->sleep_hypnogram);
    gtk_box_append(GTK_BOX(dash->sleep_content), card);
    gtk_box_append(GTK_BOX(box), dash->sleep_content);

    /* Last 7 nights (shown even when this night is empty) */
    GtkWidget *week_sub = NULL;
    card = ui_card_new(&inner, 12);
    gtk_box_append(GTK_BOX(inner), ui_card_header("Last 7 Nights", &week_sub, NULL));
    gtk_label_set_text(GTK_LABEL(week_sub), "Dashed line: 8 h goal · click a night to open it");
    dash->sleep_week = gtk_drawing_area_new();
    gtk_widget_set_size_request(dash->sleep_week, -1, 150);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(dash->sleep_week), draw_week, state, NULL);
    GtkGesture *click = gtk_gesture_click_new();
    g_signal_connect(click, "released", G_CALLBACK(on_week_click), state);
    gtk_widget_add_controller(dash->sleep_week, GTK_EVENT_CONTROLLER(click));
    gtk_box_append(GTK_BOX(inner), dash->sleep_week);
    gtk_box_append(GTK_BOX(box), card);

    GtkWidget *scroll = ui_page_scroll(box, 960);
    return scroll;
}

static void set_legend(int idx, int minutes, int total) {
    gchar d[32];
    ui_format_minutes(minutes, d, sizeof(d));
    gchar *m = g_markup_printf_escaped("%s<span weight=\"normal\" alpha=\"55%%\" size=\"small\">  %d%%</span>", d,
                                       total > 0 ? (int)lround(100.0 * minutes / total) : 0);
    gtk_label_set_markup(GTK_LABEL(legend_values[idx]), m);
    g_free(m);
}

void ui_sleep_refresh(SalusApp *state) {
    SalusDashboardUi *dash = state->dash_ui;
    if (dash == NULL || dash->sleep_content == NULL) {
        return;
    }
    UiNight *n = dash->night;
    ui_night_collect(state, ui_view_date(state), n);
    const gboolean has = n->total_asleep > 0 || n->first >= 0;
    gtk_widget_set_visible(dash->sleep_content, has);
    gtk_widget_set_visible(dash->sleep_empty, !has);

    GDate d = *ui_view_date(state);
    g_date_subtract_days(&d, 6);
    UiNight *tmp = g_new(UiNight, 1);
    for (int i = 0; i < 7; i++) {
        ui_night_collect(state, &d, tmp);
        dash->week_minutes[i] = tmp->total_asleep;
        g_date_add_days(&d, 1);
    }
    g_free(tmp);
    ui_night_collect(state, ui_view_date(state), n); /* the shared buffer holds the viewed night */
    gtk_widget_queue_draw(dash->sleep_week);
    if (!has) {
        return;
    }

    gchar buf[32];
    g_snprintf(buf, sizeof(buf), "%dh %02dm", n->total_asleep / 60, n->total_asleep % 60);
    gtk_label_set_text(GTK_LABEL(dash->sleep_total), buf);
    const int pct_goal = (int)lround(100.0 * n->total_asleep / SLEEP_GOAL_MIN);
    gchar sub[64];
    g_snprintf(sub, sizeof(sub), "Asleep · %d%% of the 8 h goal", pct_goal);
    gtk_label_set_text(GTK_LABEL(sleep_summary_sub), sub);
    if (n->first >= 0) {
        ui_format_clock(UI_NIGHT_START_MIN + n->first, buf, sizeof(buf));
        gtk_label_set_text(GTK_LABEL(sleep_bed_value), buf);
        ui_format_clock(UI_NIGHT_START_MIN + n->last + 1, buf, sizeof(buf));
        gtk_label_set_text(GTK_LABEL(sleep_wake_value), buf);
        ui_format_minutes(n->last + 1 - n->first, buf, sizeof(buf));
        gtk_label_set_text(GTK_LABEL(sleep_inbed_value), buf);
    } else {
        gtk_label_set_text(GTK_LABEL(sleep_bed_value), "—");
        gtk_label_set_text(GTK_LABEL(sleep_wake_value), "—");
        gtk_label_set_text(GTK_LABEL(sleep_inbed_value), "—");
    }
    if (n->avg_hr > 0) {
        g_snprintf(buf, sizeof(buf), "%d bpm", n->avg_hr);
        gtk_label_set_text(GTK_LABEL(sleep_hr_value), buf);
    } else {
        gtk_label_set_text(GTK_LABEL(sleep_hr_value), "—");
    }
    if (n->avg_spo2 > 0) {
        g_snprintf(buf, sizeof(buf), "%d%%", n->avg_spo2);
        gtk_label_set_text(GTK_LABEL(sleep_spo2_value), buf);
    } else {
        gtk_label_set_text(GTK_LABEL(sleep_spo2_value), "—");
    }

    gtk_widget_set_visible(dash->sleep_bar, n->has_breakdown);
    gtk_widget_set_visible(dash->sleep_legend, n->has_breakdown);
    if (n->has_breakdown) {
        const int total = n->deep + n->light + n->rem + n->awake;
        set_legend(0, n->awake, total);
        set_legend(1, n->rem, total);
        set_legend(2, n->light, total);
        set_legend(3, n->deep, total);
    }
    gtk_widget_set_visible(gtk_widget_get_parent(dash->sleep_hypnogram), n->first >= 0);
    gtk_label_set_text(GTK_LABEL(sleep_stages_sub),
                       !n->has_breakdown ? "The band didn't send sleep stages for this night"
                                         : "The band sent stage totals but no timeline for this night");
    gtk_widget_set_visible(sleep_stages_sub, n->first >= 0 && !n->has_timeline);
    gtk_widget_queue_draw(dash->sleep_bar);
    gtk_widget_queue_draw(dash->sleep_hypnogram);
}
