#include "ui_private.h"

#include "salus/store/sleep.h"

#include <math.h>
#include <string.h>

#ifndef SALUS_ICONS_DIR
#define SALUS_ICONS_DIR "src/resources/icons"
#endif

/* ---- Palette ------------------------------------------------------------------------------ */

typedef struct {
    const char *css;
    const char *light;
    const char *dark;
} UiColorDef;

static const UiColorDef ui_colors[UI_COLOR_COUNT] = {
    [UI_COLOR_HR] = {"salus-c-hr", "#e01b24", "#ff7b7b"},
    [UI_COLOR_SPO2] = {"salus-c-spo2", "#1c71d8", "#62a0ea"},
    [UI_COLOR_STRESS] = {"salus-c-stress", "#c88800", "#f6d32d"},
    [UI_COLOR_STEPS] = {"salus-c-steps", "#26a269", "#57e389"},
    [UI_COLOR_CALORIES] = {"salus-c-calories", "#e66100", "#ffa348"},
    [UI_COLOR_ACTIVE] = {"salus-c-active", "#0e8a8e", "#5bd6d6"},
    [UI_COLOR_SLEEP] = {"salus-c-sleep", "#813d9c", "#c78ae6"},
    [UI_COLOR_VITALITY] = {"salus-c-vitality", "#c01c6b", "#f66dab"},
    [UI_COLOR_WEIGHT] = {"salus-c-weight", "#865e3c", "#cdab8f"},
    [UI_COLOR_CYCLES] = {"salus-c-cycles", "#a51d2d", "#f66151"},
    [UI_COLOR_DEEP] = {"salus-c-deep", "#4a2ea8", "#9a86f5"},
    [UI_COLOR_LIGHT] = {"salus-c-light", "#3584e4", "#99c1f1"},
    [UI_COLOR_REM] = {"salus-c-rem", "#b146c2", "#dc8add"},
    [UI_COLOR_AWAKE] = {"salus-c-awake", "#d38b00", "#f9c440"},
};

gboolean ui_is_dark(void) {
    return adw_style_manager_get_dark(adw_style_manager_get_default());
}

const char *ui_color_css_class(UiColor c) {
    return (c >= 0 && c < UI_COLOR_COUNT) ? ui_colors[c].css : "";
}

void ui_color_rgb(UiColor c, double rgb[3]) {
    GdkRGBA rgba = {0};
    if (c < 0 || c >= UI_COLOR_COUNT || !gdk_rgba_parse(&rgba, ui_is_dark() ? ui_colors[c].dark : ui_colors[c].light)) {
        rgb[0] = rgb[1] = rgb[2] = 0.5;
        return;
    }
    rgb[0] = rgba.red;
    rgb[1] = rgba.green;
    rgb[2] = rgba.blue;
}

void ui_cairo_set_color(cairo_t *cr, UiColor c, double alpha) {
    double rgb[3];
    ui_color_rgb(c, rgb);
    cairo_set_source_rgba(cr, rgb[0], rgb[1], rgb[2], alpha);
}

void ui_cairo_set_fg(cairo_t *cr, GtkWidget *widget, double alpha) {
    GdkRGBA fg = {0, 0, 0, 1};
    if (widget != NULL) {
        gtk_widget_get_color(widget, &fg);
    }
    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, fg.alpha * alpha);
}

void ui_style_install(GdkDisplay *display) {
    if (display == NULL) {
        return;
    }
    GtkIconTheme *theme = gtk_icon_theme_get_for_display(display);
    gtk_icon_theme_add_search_path(theme, SALUS_ICONS_DIR);

    GString *css = g_string_new(
        ".salus-page { padding: 18px 12px 24px 12px; }"
        ".salus-card { padding: 16px; }"
        ".salus-card-title { font-weight: 700; }"
        ".salus-tile { padding: 14px 14px 10px 14px; min-height: 118px; }"
        ".salus-tile-title { font-weight: 600; font-size: 0.92em; }"
        ".salus-tile-value { font-size: 1.9em; font-weight: 800; }"
        ".salus-tile-unit { font-weight: 600; opacity: 0.6; margin-bottom: 4px; }"
        ".salus-hero-value { font-size: 2.6em; font-weight: 800; }"
        ".salus-ring-label { font-weight: 600; }"
        ".salus-ring-value { font-weight: 800; font-size: 1.25em; }"
        ".salus-stat-value { font-weight: 800; font-size: 1.15em; }"
        ".salus-bubble { border-radius: 999px; padding: 7px; background: alpha(currentColor, 0.14); }"
        ".salus-dot { border-radius: 999px; min-width: 10px; min-height: 10px; background: currentColor; }"
        ".salus-live { border-radius: 999px; padding: 2px 9px; font-size: 0.8em; font-weight: 700;"
        "  color: @accent_fg_color; background: @accent_bg_color; }"
        ".salus-date-title { font-weight: 700; font-size: 1.1em; }"
        ".salus-band-icon { border-radius: 999px; padding: 18px; background: alpha(@accent_bg_color, 0.15);"
        "  color: @accent_color; }"
        ".salus-log { font-family: monospace; font-size: 0.85em; }"
        "toggle-group.salus-metric-toggle toggle { padding-left: 4px; padding-right: 4px; }");
    for (int c = 0; c < UI_COLOR_COUNT; c++) {
        g_string_append_printf(css, ".%s { color: %s; }", ui_colors[c].css, ui_colors[c].light);
    }
    g_string_append(css, "@media (prefers-color-scheme: dark) {");
    for (int c = 0; c < UI_COLOR_COUNT; c++) {
        g_string_append_printf(css, ".%s { color: %s; }", ui_colors[c].css, ui_colors[c].dark);
    }
    g_string_append(css, "}");

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider, css->str);
    gtk_style_context_add_provider_for_display(display, GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
    g_string_free(css, TRUE);
}

/* ---- Small widget helpers ----------------------------------------------------------------- */

GtkWidget *ui_label(const char *text, const char *css_class, float xalign) {
    GtkWidget *l = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(l), xalign);
    if (css_class != NULL) {
        gchar **classes = g_strsplit(css_class, " ", -1);
        for (gchar **c = classes; *c != NULL; c++) {
            if (**c != '\0') {
                gtk_widget_add_css_class(l, *c);
            }
        }
        g_strfreev(classes);
    }
    return l;
}

GtkWidget *ui_icon(const char *icon_name, UiColor color, int pixel_size) {
    GtkWidget *img = gtk_image_new_from_icon_name(icon_name);
    gtk_image_set_pixel_size(GTK_IMAGE(img), pixel_size);
    if (color >= 0 && color < UI_COLOR_COUNT) {
        gtk_widget_add_css_class(img, ui_color_css_class(color));
    }
    return img;
}

GtkWidget *ui_card_new(GtkWidget **inner_out, int spacing) {
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, spacing);
    gtk_widget_add_css_class(card, "card");
    gtk_widget_add_css_class(card, "salus-card");
    if (inner_out != NULL) {
        *inner_out = card;
    }
    return card;
}

GtkWidget *ui_card_header(const char *title, GtkWidget **subtitle_out, GtkWidget *end_widget) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *texts = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(texts, TRUE);
    gtk_box_append(GTK_BOX(texts), ui_label(title, "heading", 0.0f));
    if (subtitle_out != NULL) {
        GtkWidget *sub = ui_label("", "caption dimmed", 0.0f);
        gtk_label_set_wrap(GTK_LABEL(sub), TRUE);
        gtk_box_append(GTK_BOX(texts), sub);
        *subtitle_out = sub;
    }
    gtk_box_append(GTK_BOX(row), texts);
    if (end_widget != NULL) {
        gtk_widget_set_valign(end_widget, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(row), end_widget);
    }
    return row;
}

GtkWidget *ui_stat_new(const char *caption, GtkWidget **value_out) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(box, TRUE);
    GtkWidget *value = ui_label("—", "salus-stat-value numeric", 0.0f);
    gtk_box_append(GTK_BOX(box), value);
    gtk_box_append(GTK_BOX(box), ui_label(caption, "caption dimmed", 0.0f));
    if (value_out != NULL) {
        *value_out = value;
    }
    return box;
}

GtkWidget *ui_stat_flow_new(void) {
    GtkWidget *flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(flow), TRUE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(flow), 2);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow), 4);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(flow), 10);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(flow), 12);
    return flow;
}

void ui_stat_flow_append(GtkWidget *flow, GtkWidget *child) {
    gtk_widget_set_can_focus(child, FALSE);
    gtk_flow_box_append(GTK_FLOW_BOX(flow), child);
    gtk_widget_set_can_focus(gtk_widget_get_parent(child), FALSE);
}

void ui_format_thousands(long v, gchar *buf, gsize len) {
    gchar raw[32];
    g_snprintf(raw, sizeof(raw), "%ld", v < 0 ? -v : v);
    const size_t n = strlen(raw);
    GString *s = g_string_new(v < 0 ? "-" : "");
    for (size_t i = 0; i < n; i++) {
        g_string_append_c(s, raw[i]);
        const size_t left = n - i - 1;
        if (left > 0 && left % 3 == 0) {
            g_string_append_c(s, ',');
        }
    }
    g_strlcpy(buf, s->str, len);
    g_string_free(s, TRUE);
}

void ui_format_minutes(int minutes, gchar *buf, gsize len) {
    if (minutes < 60) {
        g_snprintf(buf, len, "%d min", MAX(minutes, 0));
    } else {
        g_snprintf(buf, len, "%dh %02dm", minutes / 60, minutes % 60);
    }
}

void ui_format_clock(int minute_of_day, gchar *buf, gsize len) {
    minute_of_day = ((minute_of_day % UI_DAY_MINUTES) + UI_DAY_MINUTES) % UI_DAY_MINUTES;
    g_snprintf(buf, len, "%02d:%02d", minute_of_day / 60, minute_of_day % 60);
}

GtkWidget *ui_page_scroll(GtkWidget *content, int max_width) {
    GtkWidget *clamp = adw_clamp_new();
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp), max_width);
    adw_clamp_set_tightening_threshold(ADW_CLAMP(clamp), MIN(600, max_width));
    adw_clamp_set_child(ADW_CLAMP(clamp), content);
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), clamp);
    return scroll;
}

void ui_clear_box(GtkWidget *box) {
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(box)) != NULL) {
        if (GTK_IS_BOX(box)) {
            gtk_box_remove(GTK_BOX(box), child);
        } else if (GTK_IS_LIST_BOX(box)) {
            gtk_list_box_remove(GTK_LIST_BOX(box), child);
        } else {
            gtk_widget_unparent(child);
        }
    }
}

/* ---- Drawing helpers ---------------------------------------------------------------------- */

void ui_rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r) {
    r = MIN(r, MIN(w, h) / 2.0);
    if (r <= 0.0) {
        cairo_rectangle(cr, x, y, w, h);
        return;
    }
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -G_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, G_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, G_PI / 2, G_PI);
    cairo_arc(cr, x + r, y + r, r, G_PI, 3 * G_PI / 2);
    cairo_close_path(cr);
}

void ui_draw_text(cairo_t *cr, GtkWidget *widget, const char *text, double x, double y,
                  double size, double alpha, double xalign) {
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, size);
    cairo_text_extents_t ext;
    cairo_text_extents(cr, text, &ext);
    ui_cairo_set_fg(cr, widget, alpha);
    cairo_move_to(cr, x - ext.x_advance * xalign, y);
    cairo_show_text(cr, text);
}

void ui_draw_tooltip(cairo_t *cr, GtkWidget *widget, const char *text, double x, double top, double right) {
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 11.5);
    cairo_text_extents_t ext;
    cairo_text_extents(cr, text, &ext);
    const double bw = ext.x_advance + 18;
    const double bx = x + 10 + bw > right ? x - 10 - bw : x + 10;
    const gboolean dark = ui_is_dark();
    cairo_set_source_rgba(cr, dark ? 0.25 : 1, dark ? 0.25 : 1, dark ? 0.27 : 1, 0.97);
    ui_rounded_rect(cr, bx, top, bw, 24, 8);
    cairo_fill_preserve(cr);
    ui_cairo_set_fg(cr, widget, 0.12);
    cairo_stroke(cr);
    ui_cairo_set_fg(cr, widget, 0.9);
    cairo_move_to(cr, bx + 9, top + 16);
    cairo_show_text(cr, text);
}

void ui_draw_rings(cairo_t *cr, double cx, double cy, double radius, double thickness,
                   const double *ratios, const UiColor *colors, int n) {
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_width(cr, thickness);
    for (int i = 0; i < n; i++) {
        const double r = radius - thickness / 2.0 - i * (thickness + thickness * 0.28);
        if (r <= thickness / 2.0) {
            break;
        }
        ui_cairo_set_color(cr, colors[i], 0.18);
        cairo_new_path(cr);
        cairo_arc(cr, cx, cy, r, 0, 2 * G_PI);
        cairo_stroke(cr);
        const double ratio = ratios[i];
        if (ratio <= 0.0) {
            continue;
        }
        const double start = -G_PI / 2.0;
        /* Past 100 % the ring keeps going round (as on the band), capped at two laps. */
        const double sweep = 2 * G_PI * MIN(ratio, 2.0);
        ui_cairo_set_color(cr, colors[i], 1.0);
        cairo_new_path(cr);
        cairo_arc(cr, cx, cy, r, start, start + MAX(sweep, 0.02));
        cairo_stroke(cr);
    }
}

/* ---- Series ------------------------------------------------------------------------------- */

static int series_value(const RecordedSample *s, UiSeries series) {
    switch (series) {
        case UI_SERIES_HR:
            return (s->heart_rate > 0 && s->heart_rate < 240) ? s->heart_rate : -1;
        case UI_SERIES_SPO2:
            return salus_spo2_value_valid(s->spo2) ? s->spo2 : -1;
        case UI_SERIES_STRESS:
            return salus_stress_value_valid(s->stress) ? s->stress : -1;
        case UI_SERIES_STEPS:
            return s->steps > 0 ? s->steps : -1;
        default:
            return -1;
    }
}

guint ui_series_collect(AppState *state, const gchar *day_key, UiSeries series, int *values) {
    for (int m = 0; m < UI_DAY_MINUTES; m++) {
        values[m] = -1;
    }
    guint n = 0;
    g_mutex_lock(&state->recorded_lock);
    GArray *arr = state->recorded_days != NULL ? g_hash_table_lookup(state->recorded_days, day_key) : NULL;
    if (arr != NULL) {
        for (guint i = 0; i < arr->len; i++) {
            const RecordedSample *s = &g_array_index(arr, RecordedSample, i);
            const int v = series_value(s, series);
            if (v >= 0 && s->minute_of_day < UI_DAY_MINUTES) {
                values[s->minute_of_day] = v;
                n++;
            }
        }
    }
    g_mutex_unlock(&state->recorded_lock);
    return n;
}

static const UiColor series_colors[UI_SERIES_COUNT] = {
    [UI_SERIES_HR] = UI_COLOR_HR,
    [UI_SERIES_SPO2] = UI_COLOR_SPO2,
    [UI_SERIES_STRESS] = UI_COLOR_STRESS,
    [UI_SERIES_STEPS] = UI_COLOR_STEPS,
};

static void draw_sparkline(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
    AppState *state = user_data;
    const UiSeries series = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(area), "salus-series"));
    if (width <= 4 || height <= 4) {
        return;
    }
    int *values = g_new(int, UI_DAY_MINUTES);
    gchar *key = ui_day_key(ui_view_date(state));
    const guint n = ui_series_collect(state, key, series, values);
    g_free(key);
    if (n < 2) {
        g_free(values);
        return;
    }
    int lo = G_MAXINT, hi = G_MININT;
    for (int m = 0; m < UI_DAY_MINUTES; m++) {
        if (values[m] >= 0) {
            lo = MIN(lo, values[m]);
            hi = MAX(hi, values[m]);
        }
    }
    if (hi - lo < 4) {
        hi = lo + 4;
    }
    const double pad = 2.5;
    ui_cairo_set_color(cr, series_colors[series], 0.9);
    cairo_set_line_width(cr, 1.6);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    int prev = -1;
    for (int m = 0; m < UI_DAY_MINUTES; m++) {
        if (values[m] < 0) {
            continue;
        }
        const double x = pad + (width - 2 * pad) * (double)m / (UI_DAY_MINUTES - 1);
        const double y = pad + (height - 2 * pad) * (1.0 - (double)(values[m] - lo) / (hi - lo));
        if (prev >= 0 && m - prev <= 90) {
            cairo_line_to(cr, x, y);
        } else {
            cairo_move_to(cr, x, y);
        }
        prev = m;
    }
    cairo_stroke(cr);
    g_free(values);
}

GtkWidget *ui_sparkline_new(AppState *state, UiSeries series) {
    GtkWidget *area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area, -1, 26);
    gtk_widget_set_hexpand(area, TRUE);
    g_object_set_data(G_OBJECT(area), "salus-series", GINT_TO_POINTER(series));
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), draw_sparkline, state, NULL);
    return area;
}

/* ---- Sleep night window ------------------------------------------------------------------- */

static void night_copy_day(AppState *state, GDate *day, int from_min, int to_min, UiNight *out, int offset) {
    gchar *key = ui_day_key(day);
    GArray *arr = state->recorded_days != NULL ? g_hash_table_lookup(state->recorded_days, key) : NULL;
    g_free(key);
    if (arr == NULL) {
        return;
    }
    for (guint i = 0; i < arr->len; i++) {
        const RecordedSample *s = &g_array_index(arr, RecordedSample, i);
        if (s->minute_of_day < from_min || s->minute_of_day >= to_min) {
            continue;
        }
        const int at = offset + s->minute_of_day - from_min;
        if (s->sleep_stage >= 0) {
            out->stage[at] = s->sleep_stage;
        }
        if (s->heart_rate > 0 && s->heart_rate < 240) {
            out->hr_sum[at] += s->heart_rate;
            out->hr_n[at]++;
        }
        if (salus_spo2_value_valid(s->spo2)) {
            out->spo2_sum[at] += s->spo2;
            out->spo2_n[at]++;
        }
    }
}

/* Each minute weighs the same, so beat-by-beat HR bursts don't outweigh sparse readings. */
static int night_average(const int *sum, const int *n, int first, int last) {
    double total = 0;
    int minutes = 0;
    for (int i = MAX(first, 0); i <= last && i < UI_NIGHT_MINUTES; i++) {
        if (n[i] > 0) {
            total += (double)sum[i] / n[i];
            minutes++;
        }
    }
    return minutes > 0 ? (int)lround(total / minutes) : 0;
}

void ui_night_collect(AppState *state, GDate *wake_day, UiNight *out) {
    memset(out, 0, sizeof(*out));
    for (int i = 0; i < UI_NIGHT_MINUTES; i++) {
        out->stage[i] = -1;
    }
    out->first = out->last = -1;

    GDate *prev = g_date_copy(wake_day);
    g_date_subtract_days(prev, 1);
    const int tail = UI_DAY_MINUTES - UI_NIGHT_START_MIN;

    g_mutex_lock(&state->recorded_lock);
    night_copy_day(state, prev, UI_NIGHT_START_MIN, UI_DAY_MINUTES, out, 0);
    night_copy_day(state, wake_day, 0, UI_NIGHT_START_MIN, out, tail);

    gchar *wake_key = ui_day_key(wake_day);
    const SalusSleepDurationSummary *sd =
        state->sleep_duration_by_day != NULL ? g_hash_table_lookup(state->sleep_duration_by_day, wake_key) : NULL;
    SalusSleepDurationSummary band = {0};
    if (sd != NULL) {
        band = *sd;
    }
    g_free(wake_key);
    g_mutex_unlock(&state->recorded_lock);
    g_date_free(prev);

    /* A night is the band's bed → wake session when it reported one; stage markers outside it
     * (the band sometimes keeps emitting them after wake-up) are not part of the night. */
    if (band.from_db && band.wake_sec > band.bed_sec && band.bed_sec > 0) {
        GDateTime *start = g_date_time_new_local((gint)g_date_get_year(wake_day), (gint)g_date_get_month(wake_day),
                                                 (gint)g_date_get_day(wake_day), 0, 0, 0);
        GDateTime *night0 = g_date_time_add_hours(start, -(24 - UI_NIGHT_START_MIN / 60));
        const gint64 t0 = g_date_time_to_unix(night0);
        g_date_time_unref(night0);
        g_date_time_unref(start);
        const gint64 from = (band.bed_sec - t0) / 60, to = (band.wake_sec - t0 + 59) / 60;
        if (to > 0 && from < UI_NIGHT_MINUTES) {
            for (int i = 0; i < UI_NIGHT_MINUTES; i++) {
                if (i < from || i >= to) {
                    out->stage[i] = -1;
                }
            }
        }
    }

    gboolean real_stages = FALSE;
    for (int i = 0; i < UI_NIGHT_MINUTES; i++) {
        switch (out->stage[i]) {
            case 2: out->deep++; real_stages = TRUE; break;
            case 3: out->light++; real_stages = TRUE; break;
            case 4: out->rem++; real_stages = TRUE; break;
            case 5: out->awake++; real_stages = TRUE; break;
            case -1: continue;
            default: out->other++; break;
        }
        if (out->first < 0) {
            out->first = i;
        }
        out->last = i;
    }

    /* With a real stage timeline, the breakdown and the total come from it, so the bar, the
     * legend and the hypnogram agree. Otherwise the band's own totals are used when plausible. */
    const int counted = out->deep + out->light + out->rem + out->other;
    const int span = out->first >= 0 ? out->last - out->first + 1 : 0;
    out->total_asleep = counted;
    if (!real_stages && band.from_db && band.total_asleep > 0 && (span == 0 || band.total_asleep <= span + 15)) {
        out->total_asleep = band.total_asleep;
        const int db_stages = band.deep + band.light + band.rem + band.awake;
        const gboolean total_only = band.deep == 0 && band.rem == 0 && band.awake == 0;
        if (db_stages > 0 && !total_only && (span == 0 || db_stages <= span + 15)) {
            out->deep = band.deep;
            out->light = band.light;
            out->rem = band.rem;
            out->awake = band.awake;
            out->other = 0;
        }
    }
    out->has_breakdown = (out->deep + out->rem + out->awake) > 0;
    out->has_timeline = real_stages;
    if (out->first >= 0) {
        out->avg_hr = night_average(out->hr_sum, out->hr_n, out->first, out->last);
        out->avg_spo2 = night_average(out->spo2_sum, out->spo2_n, out->first, out->last);
    }
}
