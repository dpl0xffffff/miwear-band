#include "ui_private.h"

#include "miwear/sleep/mi_sleep_duration.h"

#include <math.h>
#include <string.h>

#ifndef MIWEAR_ICONS_DIR
#define MIWEAR_ICONS_DIR "src/resources/icons"
#endif

/* ---- Palette ------------------------------------------------------------------------------ */

typedef struct {
    const char *css;
    const char *light;
    const char *dark;
} MiColorDef;

static const MiColorDef mi_colors[MI_COLOR_COUNT] = {
    [MI_COLOR_HR] = {"mi-c-hr", "#e01b24", "#ff7b7b"},
    [MI_COLOR_SPO2] = {"mi-c-spo2", "#1c71d8", "#62a0ea"},
    [MI_COLOR_STRESS] = {"mi-c-stress", "#c88800", "#f6d32d"},
    [MI_COLOR_STEPS] = {"mi-c-steps", "#26a269", "#57e389"},
    [MI_COLOR_CALORIES] = {"mi-c-calories", "#e66100", "#ffa348"},
    [MI_COLOR_ACTIVE] = {"mi-c-active", "#0e8a8e", "#5bd6d6"},
    [MI_COLOR_SLEEP] = {"mi-c-sleep", "#813d9c", "#c78ae6"},
    [MI_COLOR_VITALITY] = {"mi-c-vitality", "#c01c6b", "#f66dab"},
    [MI_COLOR_WEIGHT] = {"mi-c-weight", "#865e3c", "#cdab8f"},
    [MI_COLOR_CYCLES] = {"mi-c-cycles", "#a51d2d", "#f66151"},
    [MI_COLOR_DEEP] = {"mi-c-deep", "#4a2ea8", "#9a86f5"},
    [MI_COLOR_LIGHT] = {"mi-c-light", "#3584e4", "#99c1f1"},
    [MI_COLOR_REM] = {"mi-c-rem", "#b146c2", "#dc8add"},
    [MI_COLOR_AWAKE] = {"mi-c-awake", "#d38b00", "#f9c440"},
};

gboolean mi_is_dark(void) {
    return adw_style_manager_get_dark(adw_style_manager_get_default());
}

const char *mi_color_css_class(MiColor c) {
    return (c >= 0 && c < MI_COLOR_COUNT) ? mi_colors[c].css : "";
}

void mi_color_rgb(MiColor c, double rgb[3]) {
    GdkRGBA rgba = {0};
    if (c < 0 || c >= MI_COLOR_COUNT || !gdk_rgba_parse(&rgba, mi_is_dark() ? mi_colors[c].dark : mi_colors[c].light)) {
        rgb[0] = rgb[1] = rgb[2] = 0.5;
        return;
    }
    rgb[0] = rgba.red;
    rgb[1] = rgba.green;
    rgb[2] = rgba.blue;
}

void mi_cairo_set_color(cairo_t *cr, MiColor c, double alpha) {
    double rgb[3];
    mi_color_rgb(c, rgb);
    cairo_set_source_rgba(cr, rgb[0], rgb[1], rgb[2], alpha);
}

void mi_cairo_set_fg(cairo_t *cr, GtkWidget *widget, double alpha) {
    GdkRGBA fg = {0, 0, 0, 1};
    if (widget != NULL) {
        gtk_widget_get_color(widget, &fg);
    }
    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, fg.alpha * alpha);
}

void mi_style_install(GdkDisplay *display) {
    if (display == NULL) {
        return;
    }
    GtkIconTheme *theme = gtk_icon_theme_get_for_display(display);
    gtk_icon_theme_add_search_path(theme, MIWEAR_ICONS_DIR);

    GString *css = g_string_new(
        ".mi-page { padding: 18px 12px 24px 12px; }"
        ".mi-card { padding: 16px; }"
        ".mi-card-title { font-weight: 700; }"
        ".mi-tile { padding: 14px 14px 10px 14px; min-height: 118px; }"
        ".mi-tile-title { font-weight: 600; font-size: 0.92em; }"
        ".mi-tile-value { font-size: 1.9em; font-weight: 800; }"
        ".mi-tile-unit { font-weight: 600; opacity: 0.6; margin-bottom: 4px; }"
        ".mi-hero-value { font-size: 2.6em; font-weight: 800; }"
        ".mi-ring-label { font-weight: 600; }"
        ".mi-ring-value { font-weight: 800; font-size: 1.25em; }"
        ".mi-stat-value { font-weight: 800; font-size: 1.15em; }"
        ".mi-bubble { border-radius: 999px; padding: 7px; background: alpha(currentColor, 0.14); }"
        ".mi-dot { border-radius: 999px; min-width: 10px; min-height: 10px; background: currentColor; }"
        ".mi-live { border-radius: 999px; padding: 2px 9px; font-size: 0.8em; font-weight: 700;"
        "  color: @accent_fg_color; background: @accent_bg_color; }"
        ".mi-date-title { font-weight: 700; font-size: 1.1em; }"
        ".mi-band-icon { border-radius: 999px; padding: 18px; background: alpha(@accent_bg_color, 0.15);"
        "  color: @accent_color; }"
        ".mi-log { font-family: monospace; font-size: 0.85em; }"
        "toggle-group.mi-metric-toggle toggle { padding-left: 4px; padding-right: 4px; }");
    for (int c = 0; c < MI_COLOR_COUNT; c++) {
        g_string_append_printf(css, ".%s { color: %s; }", mi_colors[c].css, mi_colors[c].light);
    }
    g_string_append(css, "@media (prefers-color-scheme: dark) {");
    for (int c = 0; c < MI_COLOR_COUNT; c++) {
        g_string_append_printf(css, ".%s { color: %s; }", mi_colors[c].css, mi_colors[c].dark);
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

GtkWidget *mi_label(const char *text, const char *css_class, float xalign) {
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

GtkWidget *mi_icon(const char *icon_name, MiColor color, int pixel_size) {
    GtkWidget *img = gtk_image_new_from_icon_name(icon_name);
    gtk_image_set_pixel_size(GTK_IMAGE(img), pixel_size);
    if (color >= 0 && color < MI_COLOR_COUNT) {
        gtk_widget_add_css_class(img, mi_color_css_class(color));
    }
    return img;
}

GtkWidget *mi_card_new(GtkWidget **inner_out, int spacing) {
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, spacing);
    gtk_widget_add_css_class(card, "card");
    gtk_widget_add_css_class(card, "mi-card");
    if (inner_out != NULL) {
        *inner_out = card;
    }
    return card;
}

GtkWidget *mi_card_header(const char *title, GtkWidget **subtitle_out, GtkWidget *end_widget) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *texts = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(texts, TRUE);
    gtk_box_append(GTK_BOX(texts), mi_label(title, "heading", 0.0f));
    if (subtitle_out != NULL) {
        GtkWidget *sub = mi_label("", "caption dimmed", 0.0f);
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

GtkWidget *mi_stat_new(const char *caption, GtkWidget **value_out) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(box, TRUE);
    GtkWidget *value = mi_label("—", "mi-stat-value numeric", 0.0f);
    gtk_box_append(GTK_BOX(box), value);
    gtk_box_append(GTK_BOX(box), mi_label(caption, "caption dimmed", 0.0f));
    if (value_out != NULL) {
        *value_out = value;
    }
    return box;
}

GtkWidget *mi_stat_flow_new(void) {
    GtkWidget *flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(flow), TRUE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(flow), 2);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow), 4);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(flow), 10);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(flow), 12);
    return flow;
}

void mi_stat_flow_append(GtkWidget *flow, GtkWidget *child) {
    gtk_widget_set_can_focus(child, FALSE);
    gtk_flow_box_append(GTK_FLOW_BOX(flow), child);
    gtk_widget_set_can_focus(gtk_widget_get_parent(child), FALSE);
}

void mi_format_thousands(long v, gchar *buf, gsize len) {
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

void mi_format_minutes(int minutes, gchar *buf, gsize len) {
    if (minutes < 60) {
        g_snprintf(buf, len, "%d min", MAX(minutes, 0));
    } else {
        g_snprintf(buf, len, "%dh %02dm", minutes / 60, minutes % 60);
    }
}

void mi_format_clock(int minute_of_day, gchar *buf, gsize len) {
    minute_of_day = ((minute_of_day % MI_DAY_MINUTES) + MI_DAY_MINUTES) % MI_DAY_MINUTES;
    g_snprintf(buf, len, "%02d:%02d", minute_of_day / 60, minute_of_day % 60);
}

void mi_clear_box(GtkWidget *box) {
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

void mi_rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r) {
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

void mi_draw_text(cairo_t *cr, GtkWidget *widget, const char *text, double x, double y,
                  double size, double alpha, double xalign) {
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, size);
    cairo_text_extents_t ext;
    cairo_text_extents(cr, text, &ext);
    mi_cairo_set_fg(cr, widget, alpha);
    cairo_move_to(cr, x - ext.x_advance * xalign, y);
    cairo_show_text(cr, text);
}

void mi_draw_rings(cairo_t *cr, double cx, double cy, double radius, double thickness,
                   const double *ratios, const MiColor *colors, int n) {
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_width(cr, thickness);
    for (int i = 0; i < n; i++) {
        const double r = radius - thickness / 2.0 - i * (thickness + thickness * 0.28);
        if (r <= thickness / 2.0) {
            break;
        }
        mi_cairo_set_color(cr, colors[i], 0.18);
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
        mi_cairo_set_color(cr, colors[i], 1.0);
        cairo_new_path(cr);
        cairo_arc(cr, cx, cy, r, start, start + MAX(sweep, 0.02));
        cairo_stroke(cr);
    }
}

/* ---- Series ------------------------------------------------------------------------------- */

static int series_value(const RecordedSample *s, MiSeries series) {
    switch (series) {
        case MI_SERIES_HR:
            return (s->heart_rate > 0 && s->heart_rate < 240) ? s->heart_rate : -1;
        case MI_SERIES_SPO2:
            return miwear_spo2_value_valid(s->spo2) ? s->spo2 : -1;
        case MI_SERIES_STRESS:
            return miwear_stress_value_valid(s->stress) ? s->stress : -1;
        case MI_SERIES_STEPS:
            return s->steps > 0 ? s->steps : -1;
        default:
            return -1;
    }
}

guint mi_series_collect(AppState *state, const gchar *day_key, MiSeries series, int *values) {
    for (int m = 0; m < MI_DAY_MINUTES; m++) {
        values[m] = -1;
    }
    guint n = 0;
    g_mutex_lock(&state->recorded_lock);
    GArray *arr = state->recorded_days != NULL ? g_hash_table_lookup(state->recorded_days, day_key) : NULL;
    if (arr != NULL) {
        for (guint i = 0; i < arr->len; i++) {
            const RecordedSample *s = &g_array_index(arr, RecordedSample, i);
            const int v = series_value(s, series);
            if (v >= 0 && s->minute_of_day < MI_DAY_MINUTES) {
                values[s->minute_of_day] = v;
                n++;
            }
        }
    }
    g_mutex_unlock(&state->recorded_lock);
    return n;
}

static const MiColor series_colors[MI_SERIES_COUNT] = {
    [MI_SERIES_HR] = MI_COLOR_HR,
    [MI_SERIES_SPO2] = MI_COLOR_SPO2,
    [MI_SERIES_STRESS] = MI_COLOR_STRESS,
    [MI_SERIES_STEPS] = MI_COLOR_STEPS,
};

static void draw_sparkline(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
    AppState *state = user_data;
    const MiSeries series = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(area), "mi-series"));
    if (width <= 4 || height <= 4) {
        return;
    }
    int *values = g_new(int, MI_DAY_MINUTES);
    gchar *key = mi_day_key(mi_view_date(state));
    const guint n = mi_series_collect(state, key, series, values);
    g_free(key);
    if (n < 2) {
        g_free(values);
        return;
    }
    int lo = G_MAXINT, hi = G_MININT;
    for (int m = 0; m < MI_DAY_MINUTES; m++) {
        if (values[m] >= 0) {
            lo = MIN(lo, values[m]);
            hi = MAX(hi, values[m]);
        }
    }
    if (hi - lo < 4) {
        hi = lo + 4;
    }
    const double pad = 2.5;
    mi_cairo_set_color(cr, series_colors[series], 0.9);
    cairo_set_line_width(cr, 1.6);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    int prev = -1;
    for (int m = 0; m < MI_DAY_MINUTES; m++) {
        if (values[m] < 0) {
            continue;
        }
        const double x = pad + (width - 2 * pad) * (double)m / (MI_DAY_MINUTES - 1);
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

GtkWidget *mi_sparkline_new(AppState *state, MiSeries series) {
    GtkWidget *area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area, -1, 26);
    gtk_widget_set_hexpand(area, TRUE);
    g_object_set_data(G_OBJECT(area), "mi-series", GINT_TO_POINTER(series));
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), draw_sparkline, state, NULL);
    return area;
}

/* ---- Sleep night window ------------------------------------------------------------------- */

static void night_copy_day(AppState *state, GDate *day, int from_min, int to_min, MiNight *out, int offset) {
    gchar *key = mi_day_key(day);
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
        if (miwear_spo2_value_valid(s->spo2)) {
            out->spo2_sum[at] += s->spo2;
            out->spo2_n[at]++;
        }
    }
}

/* Each minute weighs the same, so beat-by-beat HR bursts don't outweigh sparse readings. */
static int night_average(const int *sum, const int *n, int first, int last) {
    double total = 0;
    int minutes = 0;
    for (int i = MAX(first, 0); i <= last && i < MI_NIGHT_MINUTES; i++) {
        if (n[i] > 0) {
            total += (double)sum[i] / n[i];
            minutes++;
        }
    }
    return minutes > 0 ? (int)lround(total / minutes) : 0;
}

void mi_night_collect(AppState *state, GDate *wake_day, MiNight *out) {
    memset(out, 0, sizeof(*out));
    for (int i = 0; i < MI_NIGHT_MINUTES; i++) {
        out->stage[i] = -1;
    }
    out->first = out->last = -1;

    GDate *prev = g_date_copy(wake_day);
    g_date_subtract_days(prev, 1);
    const int tail = MI_DAY_MINUTES - MI_NIGHT_START_MIN;

    g_mutex_lock(&state->recorded_lock);
    night_copy_day(state, prev, MI_NIGHT_START_MIN, MI_DAY_MINUTES, out, 0);
    night_copy_day(state, wake_day, 0, MI_NIGHT_START_MIN, out, tail);

    gchar *wake_key = mi_day_key(wake_day);
    const MiwearSleepDurationSummary *sd =
        state->sleep_duration_by_day != NULL ? g_hash_table_lookup(state->sleep_duration_by_day, wake_key) : NULL;
    MiwearSleepDurationSummary band = {0};
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
        GDateTime *night0 = g_date_time_add_hours(start, -(24 - MI_NIGHT_START_MIN / 60));
        const gint64 t0 = g_date_time_to_unix(night0);
        g_date_time_unref(night0);
        g_date_time_unref(start);
        const gint64 from = (band.bed_sec - t0) / 60, to = (band.wake_sec - t0 + 59) / 60;
        if (to > 0 && from < MI_NIGHT_MINUTES) {
            for (int i = 0; i < MI_NIGHT_MINUTES; i++) {
                if (i < from || i >= to) {
                    out->stage[i] = -1;
                }
            }
        }
    }

    gboolean real_stages = FALSE;
    for (int i = 0; i < MI_NIGHT_MINUTES; i++) {
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
    if (out->first >= 0) {
        out->avg_hr = night_average(out->hr_sum, out->hr_n, out->first, out->last);
        out->avg_spo2 = night_average(out->spo2_sum, out->spo2_n, out->first, out->last);
    }
}
