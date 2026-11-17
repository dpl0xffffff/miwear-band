#include "ui_private.h"

#include "miwear/activity/activity.h"
#include "miwear/activity/workout_summary.h"
#include "miwear/db/workout_store.h"

#include <math.h>
#include <string.h>

/* ======================================================================================== */
/* Sport entry model (backend-facing API)                                                   */
/* ======================================================================================== */

static MiwearSportEntry *sport_find_entry(AppState *state, uint32_t ts, int subtype) {
    for (GList *l = state != NULL ? state->sport_entries : NULL; l != NULL; l = l->next) {
        MiwearSportEntry *e = l->data;
        if (e->ts_sec == ts && e->sport_subtype == subtype) {
            return e;
        }
    }
    return NULL;
}

static void sport_entry_apply_summary(MiwearSportEntry *e, const MiwearWorkoutSummary *summary) {
    if (e == NULL || summary == NULL) {
        return;
    }
    e->start_sec = summary->start_sec;
    e->end_sec = summary->end_sec;
    e->duration_sec = summary->duration_sec;
    e->distance_m = summary->distance_m;
    e->calories = summary->calories;
    e->hr_avg = summary->hr_avg;
    g_free(e->label);
    e->label = g_strdup(summary->sport_name != NULL ? summary->sport_name
                                                    : miwear_sport_subtype_name(e->sport_subtype));
    if (e->detail != NULL) {
        miwear_workout_summary_clear(e->detail);
        g_free(e->detail);
    }
    e->detail = miwear_workout_summary_dup(summary);
}

static int sport_entry_time_cmp(gconstpointer a, gconstpointer b) {
    const MiwearSportEntry *ea = a;
    const MiwearSportEntry *eb = b;
    const uint32_t ta = ea->start_sec > 0 ? ea->start_sec : ea->ts_sec;
    const uint32_t tb = eb->start_sec > 0 ? eb->start_sec : eb->ts_sec;
    return ta > tb ? -1 : ta < tb ? 1 : 0;
}

void miwear_sport_entries_clear(AppState *state) {
    if (state == NULL) {
        return;
    }
    /* A pushed detail page draws from an entry: close it before the entries are freed
     * (a reconnect reloads them from the DB while a workout may be open). */
    MiwearDashboardUi *dash = state->dash_ui;
    if (dash != NULL && dash->sport_detail_entry != NULL) {
        dash->sport_detail_entry = NULL;
        if (dash->nav_view != NULL) {
            adw_navigation_view_pop_to_tag(ADW_NAVIGATION_VIEW(dash->nav_view), "main");
        }
    }
    for (GList *l = state->sport_entries; l != NULL; l = l->next) {
        MiwearSportEntry *e = l->data;
        g_free(e->label);
        if (e->detail != NULL) {
            miwear_workout_summary_clear(e->detail);
            g_free(e->detail);
        }
        g_free(e);
    }
    g_list_free(state->sport_entries);
    state->sport_entries = NULL;
}

static gboolean sport_load_row_cb(const MiwearDbSportWorkoutRow *row, gpointer user_data) {
    AppState *state = user_data;
    if (state == NULL || row == NULL || row->detail_json == NULL) {
        return TRUE;
    }
    MiwearWorkoutSummary sum = {0};
    int sport_type = 1;
    if (!miwear_workout_summary_from_json(row->detail_json, &sport_type, &sum)) {
        return TRUE;
    }
    if (sum.file_ts_sec == 0) {
        sum.file_ts_sec = row->file_ts;
    }
    if (sum.sport_subtype == 0) {
        sum.sport_subtype = row->sport_subtype;
    }
    MiwearSportEntry *e = g_new0(MiwearSportEntry, 1);
    e->ts_sec = row->file_ts;
    e->sport_type = sport_type > 0 ? sport_type : row->sport_type;
    e->sport_subtype = row->sport_subtype;
    sport_entry_apply_summary(e, &sum);
    if ((e->sport_subtype == 22 || e->sport_subtype == 2) && e->detail != NULL && !e->detail->has_walk_detail &&
        (e->detail->duration_sec > 0 || e->detail->distance_m > 0)) {
        e->detail->has_walk_detail = TRUE;
        if (e->detail->walk_avg_pace_sec_per_km <= 0 && e->detail->distance_m > 0 && e->detail->duration_sec > 0) {
            e->detail->walk_avg_pace_sec_per_km =
                (int)((uint64_t)e->detail->duration_sec * 1000u / (uint32_t)e->detail->distance_m);
        }
    }
    miwear_workout_summary_clear(&sum);
    state->sport_entries = g_list_prepend(state->sport_entries, e);
    return TRUE;
}

void miwear_sport_load_from_db(AppState *state) {
    if (state == NULL || state->selected_address == NULL || state->selected_address[0] == '\0') {
        return;
    }
    if (!miwear_db_ensure_device(state->selected_address)) {
        return;
    }
    miwear_sport_entries_clear(state);
    (void)miwear_db_foreach_sport_workout(sport_load_row_cb, state);
    miwear_sport_rebuild_list(state);
}

void miwear_sport_add_entry(AppState *state, uint32_t ts, int type, int subtype, const MiwearWorkoutSummary *summary) {
    MiwearSportEntry *e = sport_find_entry(state, ts, subtype);
    if (e == NULL) {
        e = g_new0(MiwearSportEntry, 1);
        e->ts_sec = ts;
        e->sport_type = type;
        e->sport_subtype = subtype;
        if (summary != NULL) {
            sport_entry_apply_summary(e, summary);
        } else {
            e->label = g_strdup(activity_subtype_name(type, subtype));
        }
        state->sport_entries = g_list_prepend(state->sport_entries, e);
    } else if (summary != NULL) {
        sport_entry_apply_summary(e, summary);
    }
    if (summary != NULL && state->selected_address != NULL && state->selected_address[0] != '\0') {
        if (miwear_db_ensure_device(state->selected_address)) {
            (void)miwear_db_replace_sport_workout(type, ts, subtype, summary);
        }
    }
    miwear_sport_rebuild_list(state);
}

void miwear_sport_merge_attachment(AppState *state, uint32_t ts, int subtype, MiwearWorkoutSummary *patch) {
    if (state == NULL || patch == NULL) {
        return;
    }
    MiwearSportEntry *e = sport_find_entry(state, ts, subtype);
    if (e == NULL) {
        miwear_sport_add_entry(state, ts, 1, subtype, NULL);
        e = sport_find_entry(state, ts, subtype);
    }
    if (e == NULL) {
        return;
    }
    if (e->detail == NULL) {
        e->detail = g_new0(MiwearWorkoutSummary, 1);
        e->detail->sport_subtype = subtype;
        e->detail->file_ts_sec = ts;
        e->detail->sport_name = g_strdup(miwear_sport_subtype_name(subtype));
    }
    MiwearWorkoutSummary *d = e->detail;
    if (patch->record_samples != NULL && patch->record_samples->len > 0) {
        if (d->record_samples != NULL) {
            g_array_unref(d->record_samples);
        }
        d->record_samples = g_array_ref(patch->record_samples);
        d->record_interval_sec = patch->record_interval_sec;
    }
    if (patch->gps_track != NULL && patch->gps_track->len > 0) {
        if (d->gps_track != NULL) {
            g_array_unref(d->gps_track);
        }
        d->gps_track = g_array_ref(patch->gps_track);
    }
    if (state->selected_address != NULL && state->selected_address[0] != '\0' &&
        miwear_db_ensure_device(state->selected_address) && d->duration_sec > 0) {
        (void)miwear_db_replace_sport_workout(e->sport_type > 0 ? e->sport_type : 1, ts, subtype, d);
    }
    miwear_sport_rebuild_list(state);
    miwear_dashboard_schedule_refresh(state);
}

/* ======================================================================================== */
/* Presentation helpers                                                                     */
/* ======================================================================================== */

static const char *sport_icon(const MiwearSportEntry *e) {
    const char *l = e->label != NULL ? e->label : "";
    gchar *low = g_ascii_strdown(l, -1);
    const char *icon = "miwear-run-symbolic";
    if (strstr(low, "swim") != NULL) {
        icon = "miwear-swim-symbolic";
    } else if (strstr(low, "cycl") != NULL || strstr(low, "bik") != NULL) {
        icon = "miwear-bike-symbolic";
    } else if (strstr(low, "walk") != NULL || strstr(low, "hik") != NULL) {
        icon = "miwear-walk-symbolic";
    } else if (strstr(low, "run") == NULL && strstr(low, "treadmill") == NULL) {
        icon = "miwear-dumbbell-symbolic";
    }
    g_free(low);
    return icon;
}

static uint32_t entry_start(const MiwearSportEntry *e) {
    return e->start_sec > 0 ? e->start_sec : e->ts_sec;
}

static const MiwearWorkoutSummary *entry_detail(const MiwearSportEntry *e) {
    return e != NULL ? e->detail : NULL;
}

static void format_distance(int m, gchar *buf, gsize len) {
    if (m <= 0) {
        g_strlcpy(buf, "—", len);
    } else if (m >= 1000) {
        g_snprintf(buf, len, "%.2f km", m / 1000.0);
    } else {
        g_snprintf(buf, len, "%d m", m);
    }
}

static void format_duration(int sec, gchar *buf, gsize len) {
    if (sec <= 0) {
        g_strlcpy(buf, "—", len);
    } else if (sec >= 3600) {
        g_snprintf(buf, len, "%d:%02d:%02d", sec / 3600, (sec / 60) % 60, sec % 60);
    } else {
        g_snprintf(buf, len, "%d:%02d", sec / 60, sec % 60);
    }
}

/* ======================================================================================== */
/* Detail page                                                                              */
/* ======================================================================================== */

static const MiColor zone_colors[5] = {MI_COLOR_SPO2, MI_COLOR_STEPS, MI_COLOR_STRESS, MI_COLOR_CALORIES,
                                       MI_COLOR_HR};

static int zone_for_hr(const MiwearWorkoutHrZones *z, int hr) {
    if (z == NULL || z->zone1_lower_bpm <= 0) {
        return -1;
    }
    const int lows[5] = {z->zone1_lower_bpm, z->zone2_lower_bpm, z->zone3_lower_bpm, z->zone4_lower_bpm,
                         z->zone5_lower_bpm};
    int zone = -1;
    for (int i = 0; i < 5; i++) {
        if (lows[i] > 0 && hr >= lows[i]) {
            zone = i;
        }
    }
    return zone;
}

static void draw_detail_hr(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
    MiwearDashboardUi *dash = user_data;
    GtkWidget *w = GTK_WIDGET(area);
    const MiwearWorkoutSummary *d = entry_detail(dash->sport_detail_entry);
    if (d == NULL || d->record_samples == NULL || d->record_samples->len < 2 || width < 80) {
        return;
    }
    const guint n = d->record_samples->len;
    int lo = 255, hi = 0;
    for (guint i = 0; i < n; i++) {
        const MiwearWorkoutSample *s = &g_array_index(d->record_samples, MiwearWorkoutSample, i);
        if (s->hr > 0) {
            lo = MIN(lo, (int)s->hr);
            hi = MAX(hi, (int)s->hr);
        }
    }
    if (hi <= 0) {
        return;
    }
    lo = MAX(0, ((lo - 5) / 10) * 10);
    hi = ((hi + 14) / 10) * 10;
    const double left = 34, right = 6, top = 6, bottom = 22;
    const double pw = width - left - right, ph = height - top - bottom;
    const uint32_t t_end = g_array_index(d->record_samples, MiwearWorkoutSample, n - 1).offset_sec;
    const double tspan = MAX((double)t_end, 1.0);
#define DX(t) (left + pw * (double)(t) / tspan)
#define DY(v) (top + ph * (1.0 - ((double)(v) - lo) / (double)(hi - lo)))

    for (int k = 0; k <= 4; k++) {
        const int v = lo + (hi - lo) * k / 4;
        mi_cairo_set_fg(cr, w, k == 0 ? 0.18 : 0.07);
        cairo_set_line_width(cr, 1);
        cairo_move_to(cr, left, round(DY(v)) + 0.5);
        cairo_line_to(cr, left + pw, round(DY(v)) + 0.5);
        cairo_stroke(cr);
        gchar t[8];
        g_snprintf(t, sizeof(t), "%d", v);
        mi_draw_text(cr, w, t, left - 6, DY(v) + 3.5, 10.5, 0.55, 1.0);
    }
    const int minutes = (int)(t_end / 60);
    const int step = minutes > 90 ? 30 : minutes > 40 ? 15 : minutes > 15 ? 5 : 1;
    for (int m = 0; m <= minutes; m += step) {
        gchar t[16];
        g_snprintf(t, sizeof(t), "%d′", m);
        mi_draw_text(cr, w, t, CLAMP(DX(m * 60), left + 8, left + pw - 8), top + ph + 16, 10.5, 0.55, 0.5);
    }

    /* Line coloured by heart-rate zone. */
    cairo_set_line_width(cr, 2.2);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    const MiwearWorkoutSample *prev = NULL;
    for (guint i = 0; i < n; i++) {
        const MiwearWorkoutSample *s = &g_array_index(d->record_samples, MiwearWorkoutSample, i);
        if (s->hr == 0) {
            prev = NULL;
            continue;
        }
        if (prev != NULL) {
            const int z = zone_for_hr(&d->hr_zones, s->hr);
            if (z >= 0) {
                mi_cairo_set_color(cr, zone_colors[z], 1.0);
            } else {
                mi_cairo_set_color(cr, MI_COLOR_HR, 1.0);
            }
            cairo_move_to(cr, DX(prev->offset_sec), DY(prev->hr));
            cairo_line_to(cr, DX(s->offset_sec), DY(s->hr));
            cairo_stroke(cr);
        }
        prev = s;
    }
    if (d->hr_avg > 0) {
        const double dashes[] = {4, 4};
        cairo_set_dash(cr, dashes, 2, 0);
        mi_cairo_set_fg(cr, w, 0.45);
        cairo_set_line_width(cr, 1);
        cairo_move_to(cr, left, round(DY(d->hr_avg)) + 0.5);
        cairo_line_to(cr, left + pw, round(DY(d->hr_avg)) + 0.5);
        cairo_stroke(cr);
        cairo_set_dash(cr, NULL, 0, 0);
    }
#undef DX
#undef DY
}

static void draw_detail_steps(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
    MiwearDashboardUi *dash = user_data;
    GtkWidget *w = GTK_WIDGET(area);
    const MiwearWorkoutSummary *d = entry_detail(dash->sport_detail_entry);
    if (d == NULL || d->record_samples == NULL || d->record_samples->len == 0 || width < 80) {
        return;
    }
    const guint n = d->record_samples->len;
    int smax = 1;
    for (guint i = 0; i < n; i++) {
        smax = MAX(smax, (int)g_array_index(d->record_samples, MiwearWorkoutSample, i).steps_delta);
    }
    const double left = 34, top = 6, bottom = 8;
    const double pw = width - left, ph = height - top - bottom;
    gchar t[16];
    g_snprintf(t, sizeof(t), "%d", smax);
    mi_draw_text(cr, w, t, left - 6, top + 8, 10.5, 0.55, 1.0);
    mi_draw_text(cr, w, "0", left - 6, top + ph, 10.5, 0.55, 1.0);
    const double bw = pw / n;
    mi_cairo_set_color(cr, MI_COLOR_STEPS, 0.9);
    for (guint i = 0; i < n; i++) {
        const int v = g_array_index(d->record_samples, MiwearWorkoutSample, i).steps_delta;
        if (v <= 0) {
            continue;
        }
        const double bh = ph * v / smax;
        mi_rounded_rect(cr, left + bw * i + bw * 0.1, top + ph - bh, MAX(bw * 0.8, 1), bh, MIN(2.5, bw * 0.4));
        cairo_fill(cr);
    }
}

static void draw_detail_route(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
    (void)area;
    MiwearDashboardUi *dash = user_data;
    const MiwearWorkoutSummary *d = entry_detail(dash->sport_detail_entry);
    if (d == NULL || d->gps_track == NULL || d->gps_track->len < 2) {
        return;
    }
    double min_lat = 90, max_lat = -90, min_lon = 180, max_lon = -180;
    for (guint i = 0; i < d->gps_track->len; i++) {
        const MiwearWorkoutGpsPoint *p = &g_array_index(d->gps_track, MiwearWorkoutGpsPoint, i);
        min_lat = MIN(min_lat, p->lat);
        max_lat = MAX(max_lat, p->lat);
        min_lon = MIN(min_lon, p->lon);
        max_lon = MAX(max_lon, p->lon);
    }
    /* Equirectangular with latitude correction keeps the route's real shape. */
    const double kx = cos((min_lat + max_lat) / 2.0 * G_PI / 180.0);
    const double gw = MAX((max_lon - min_lon) * kx, 1e-6), gh = MAX(max_lat - min_lat, 1e-6);
    const double pad = 18;
    const double scale = MIN((width - 2 * pad) / gw, (height - 2 * pad) / gh);
    const double ox = (width - gw * scale) / 2, oy = (height - gh * scale) / 2;
#define RX(p) (ox + ((p)->lon - min_lon) * kx * scale)
#define RY(p) (oy + (max_lat - (p)->lat) * scale)
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 0) {
            mi_cairo_set_color(cr, MI_COLOR_STEPS, 0.25);
            cairo_set_line_width(cr, 8);
        } else {
            mi_cairo_set_color(cr, MI_COLOR_STEPS, 1.0);
            cairo_set_line_width(cr, 3);
        }
        for (guint i = 0; i < d->gps_track->len; i++) {
            const MiwearWorkoutGpsPoint *p = &g_array_index(d->gps_track, MiwearWorkoutGpsPoint, i);
            if (i == 0) {
                cairo_move_to(cr, RX(p), RY(p));
            } else {
                cairo_line_to(cr, RX(p), RY(p));
            }
        }
        cairo_stroke(cr);
    }
    const MiwearWorkoutGpsPoint *a = &g_array_index(d->gps_track, MiwearWorkoutGpsPoint, 0);
    const MiwearWorkoutGpsPoint *b = &g_array_index(d->gps_track, MiwearWorkoutGpsPoint, d->gps_track->len - 1);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_arc(cr, RX(a), RY(a), 7, 0, 2 * G_PI);
    cairo_fill(cr);
    mi_cairo_set_color(cr, MI_COLOR_STEPS, 1.0);
    cairo_arc(cr, RX(a), RY(a), 5, 0, 2 * G_PI);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_arc(cr, RX(b), RY(b), 7, 0, 2 * G_PI);
    cairo_fill(cr);
    mi_cairo_set_color(cr, MI_COLOR_HR, 1.0);
    cairo_arc(cr, RX(b), RY(b), 5, 0, 2 * G_PI);
    cairo_fill(cr);
#undef RX
#undef RY
}

static void add_stat_tile(GtkWidget *flow, const char *caption, const char *value) {
    if (value == NULL || value[0] == '\0' || g_strcmp0(value, "—") == 0) {
        return;
    }
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_add_css_class(box, "card");
    gtk_widget_add_css_class(box, "mi-card");
    gtk_box_append(GTK_BOX(box), mi_label(caption, "caption dimmed", 0.0f));
    gtk_box_append(GTK_BOX(box), mi_label(value, "mi-stat-value numeric", 0.0f));
    gtk_flow_box_append(GTK_FLOW_BOX(flow), box);
}

static GtkWidget *zones_card(const MiwearWorkoutHrZones *z) {
    const int secs[5] = {z->zone1_sec, z->zone2_sec, z->zone3_sec, z->zone4_sec, z->zone5_sec};
    const int lows[5] = {z->zone1_lower_bpm, z->zone2_lower_bpm, z->zone3_lower_bpm, z->zone4_lower_bpm,
                         z->zone5_lower_bpm};
    static const char *names[5] = {"Warm-up", "Fat burn", "Aerobic", "Anaerobic", "Maximum"};
    int total = 0, max = 1;
    for (int i = 0; i < 5; i++) {
        total += secs[i];
        max = MAX(max, secs[i]);
    }
    if (total <= 0) {
        return NULL;
    }
    GtkWidget *inner = NULL;
    GtkWidget *card = mi_card_new(&inner, 10);
    gtk_box_append(GTK_BOX(inner), mi_card_header("Heart Rate Zones", NULL, NULL));
    for (int i = 4; i >= 0; i--) {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        GtkWidget *names_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_set_size_request(names_box, 92, -1);
        gtk_box_append(GTK_BOX(names_box), mi_label(names[i], NULL, 0.0f));
        gchar range[32];
        g_snprintf(range, sizeof(range), "≥ %d bpm", lows[i]);
        gtk_box_append(GTK_BOX(names_box), mi_label(range, "caption dimmed", 0.0f));
        GtkWidget *bar = gtk_level_bar_new_for_interval(0, max);
        gtk_level_bar_set_value(GTK_LEVEL_BAR(bar), secs[i]);
        gtk_level_bar_remove_offset_value(GTK_LEVEL_BAR(bar), GTK_LEVEL_BAR_OFFSET_LOW);
        gtk_level_bar_remove_offset_value(GTK_LEVEL_BAR(bar), GTK_LEVEL_BAR_OFFSET_HIGH);
        gtk_level_bar_remove_offset_value(GTK_LEVEL_BAR(bar), GTK_LEVEL_BAR_OFFSET_FULL);
        gtk_widget_add_css_class(bar, mi_color_css_class(zone_colors[i]));
        gtk_widget_set_hexpand(bar, TRUE);
        gtk_widget_set_valign(bar, GTK_ALIGN_CENTER);
        gchar t[32];
        g_snprintf(t, sizeof(t), "%d min · %d%%", secs[i] / 60, (int)lround(100.0 * secs[i] / total));
        GtkWidget *tl = mi_label(t, "numeric", 1.0f);
        gtk_widget_set_size_request(tl, 96, -1);
        gtk_box_append(GTK_BOX(row), names_box);
        gtk_box_append(GTK_BOX(row), bar);
        gtk_box_append(GTK_BOX(row), tl);
        gtk_box_append(GTK_BOX(inner), row);
    }
    return card;
}

static void on_detail_hidden(AdwNavigationPage *page, gpointer user_data) {
    (void)page;
    MiwearDashboardUi *dash = user_data;
    dash->sport_detail_entry = NULL;
    dash->sport_detail_page = NULL;
}

static void open_detail(AppState *state, MiwearSportEntry *e) {
    MiwearDashboardUi *dash = state->dash_ui;
    const MiwearWorkoutSummary *d = entry_detail(e);
    dash->sport_detail_entry = e;

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(box, "mi-page");

    /* Hero */
    GtkWidget *inner = NULL;
    GtkWidget *hero = mi_card_new(&inner, 14);
    GtkWidget *head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *icon = mi_icon(sport_icon(e), MI_COLOR_STEPS, 24);
    gtk_widget_add_css_class(icon, "mi-bubble");
    gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
    GtkWidget *texts = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_append(GTK_BOX(texts), mi_label(e->label != NULL ? e->label : "Workout", "title-3", 0.0f));
    GDateTime *dt = g_date_time_new_from_unix_local((gint64)entry_start(e));
    gchar *when = g_date_time_format(dt, "%A, %-d %B %Y · %H:%M");
    g_date_time_unref(dt);
    gtk_box_append(GTK_BOX(texts), mi_label(when, "dimmed", 0.0f));
    gtk_box_append(GTK_BOX(head), icon);
    gtk_box_append(GTK_BOX(head), texts);
    gtk_box_append(GTK_BOX(inner), head);

    const int duration = d != NULL && d->duration_sec > 0 ? d->duration_sec : e->duration_sec;
    const int distance = d != NULL && d->distance_m > 0 ? d->distance_m : e->distance_m;
    const int calories = d != NULL && d->calories > 0 ? d->calories : e->calories;
    const int hr_avg = d != NULL && d->hr_avg > 0 ? d->hr_avg : e->hr_avg;
    GtkWidget *strip = mi_stat_flow_new();
    GtkWidget *v;
    gchar buf[48];
    mi_stat_flow_append(strip, mi_stat_new("Duration", &v));
    format_duration(duration, buf, sizeof(buf));
    gtk_label_set_text(GTK_LABEL(v), buf);
    mi_stat_flow_append(strip, mi_stat_new("Distance", &v));
    format_distance(distance, buf, sizeof(buf));
    gtk_label_set_text(GTK_LABEL(v), buf);
    mi_stat_flow_append(strip, mi_stat_new("Calories", &v));
    g_snprintf(buf, sizeof(buf), calories > 0 ? "%d kcal" : "—", calories);
    gtk_label_set_text(GTK_LABEL(v), buf);
    mi_stat_flow_append(strip, mi_stat_new("Avg heart rate", &v));
    g_snprintf(buf, sizeof(buf), hr_avg > 0 ? "%d bpm" : "—", hr_avg);
    gtk_label_set_text(GTK_LABEL(v), buf);
    gtk_box_append(GTK_BOX(inner), strip);
    gtk_box_append(GTK_BOX(box), hero);

    /* Details */
    GtkWidget *flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(flow), TRUE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(flow), 2);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow), 4);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(flow), 12);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(flow), 12);
    if (d != NULL) {
        gchar t[48];
        if (d->hr_max > 0) {
            g_snprintf(t, sizeof(t), "%d bpm", d->hr_max);
            add_stat_tile(flow, "Max heart rate", t);
        }
        if (d->has_swim_detail) {
            g_snprintf(t, sizeof(t), "%d", d->swim_lengths);
            add_stat_tile(flow, "Lengths", d->swim_lengths > 0 ? t : NULL);
            miwear_format_swim_pace(d->swim_avg_pace_sec_per_100m, t, sizeof(t));
            if (d->swim_avg_pace_sec_per_100m > 0) {
                gchar p[64];
                g_snprintf(p, sizeof(p), "%s /100 m", t);
                add_stat_tile(flow, "Avg pace", p);
            }
            add_stat_tile(flow, "Main stroke", miwear_swim_stroke_name(d->swim_main_stroke));
            g_snprintf(t, sizeof(t), "%d", d->swim_avg_swolf);
            add_stat_tile(flow, "Avg SWOLF", d->swim_avg_swolf > 0 ? t : NULL);
            g_snprintf(t, sizeof(t), "%d", d->swim_total_strokes);
            add_stat_tile(flow, "Strokes", d->swim_total_strokes > 0 ? t : NULL);
            g_snprintf(t, sizeof(t), "%d", d->swim_avg_spm);
            add_stat_tile(flow, "Strokes / min", d->swim_avg_spm > 0 ? t : NULL);
            g_snprintf(t, sizeof(t), "%d", d->swim_training_load);
            add_stat_tile(flow, "Training load", d->swim_training_load > 0 ? t : NULL);
            miwear_format_recovery_duration(d->swim_recovery_min, t, sizeof(t));
            add_stat_tile(flow, "Recovery", d->swim_recovery_min > 0 ? t : NULL);
            g_snprintf(t, sizeof(t), "+%d", d->swim_vitality_points);
            add_stat_tile(flow, "Vitality", d->swim_vitality_points > 0 ? t : NULL);
        } else {
            gchar pace[32];
            int pace_sec = d->walk_avg_pace_sec_per_km;
            if (pace_sec <= 0 && distance > 0 && duration > 0) {
                pace_sec = (int)((uint64_t)duration * 1000u / (uint32_t)distance);
            }
            miwear_format_walk_pace(pace_sec, pace, sizeof(pace));
            if (pace_sec > 0) {
                g_snprintf(t, sizeof(t), "%s /km", pace);
                add_stat_tile(flow, "Avg pace", t);
            }
            if (d->walk_avg_speed_kmh > 0.05f) {
                g_snprintf(t, sizeof(t), "%.1f km/h", (double)d->walk_avg_speed_kmh);
                add_stat_tile(flow, "Avg speed", t);
            }
            if (d->walk_steps > 0) {
                mi_format_thousands(d->walk_steps, t, sizeof(t));
                add_stat_tile(flow, "Steps", t);
            }
            g_snprintf(t, sizeof(t), "%d spm", d->walk_avg_cadence_spm);
            add_stat_tile(flow, "Avg cadence", d->walk_avg_cadence_spm > 0 ? t : NULL);
            g_snprintf(t, sizeof(t), "%d spm", d->walk_max_cadence_spm);
            add_stat_tile(flow, "Max cadence", d->walk_max_cadence_spm > 0 ? t : NULL);
            g_snprintf(t, sizeof(t), "%d cm", d->walk_step_length_cm);
            add_stat_tile(flow, "Step length", d->walk_step_length_cm > 0 ? t : NULL);
            g_snprintf(t, sizeof(t), "%.1f", (double)d->walk_te_aerobic);
            add_stat_tile(flow, "Aerobic effect", d->walk_te_aerobic > 0.05f ? t : NULL);
            g_snprintf(t, sizeof(t), "%.1f", (double)d->walk_te_anaerobic);
            add_stat_tile(flow, "Anaerobic effect", d->walk_te_anaerobic > 0.05f ? t : NULL);
            g_snprintf(t, sizeof(t), "%d", d->walk_training_load);
            add_stat_tile(flow, "Training load", d->walk_training_load > 0 ? t : NULL);
            miwear_format_recovery_duration(d->walk_recovery_min, t, sizeof(t));
            add_stat_tile(flow, "Recovery", d->walk_recovery_min > 0 ? t : NULL);
            g_snprintf(t, sizeof(t), "+%d", d->walk_vitality_points);
            add_stat_tile(flow, "Vitality", d->walk_vitality_points > 0 ? t : NULL);
        }
    }
    if (gtk_widget_get_first_child(flow) != NULL) {
        gtk_box_append(GTK_BOX(box), flow);
    }

    if (d != NULL && d->record_samples != NULL && d->record_samples->len >= 2) {
        GtkWidget *sub = NULL;
        GtkWidget *card = mi_card_new(&inner, 10);
        gtk_box_append(GTK_BOX(inner), mi_card_header("Heart Rate", &sub, NULL));
        g_snprintf(buf, sizeof(buf), "avg %d · max %d · min %d bpm · dashed line = average", d->hr_avg, d->hr_max,
                   d->hr_min);
        gtk_label_set_text(GTK_LABEL(sub), buf);
        GtkWidget *area = gtk_drawing_area_new();
        gtk_widget_set_size_request(area, -1, 190);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), draw_detail_hr, dash, NULL);
        gtk_box_append(GTK_BOX(inner), area);
        gtk_box_append(GTK_BOX(box), card);

        card = mi_card_new(&inner, 10);
        gtk_box_append(GTK_BOX(inner), mi_card_header("Steps", &sub, NULL));
        gtk_label_set_text(GTK_LABEL(sub), "Steps per recording interval");
        area = gtk_drawing_area_new();
        gtk_widget_set_size_request(area, -1, 110);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), draw_detail_steps, dash, NULL);
        gtk_box_append(GTK_BOX(inner), area);
        gtk_box_append(GTK_BOX(box), card);
    }
    if (d != NULL) {
        GtkWidget *zc = zones_card(&d->hr_zones);
        if (zc != NULL) {
            gtk_box_append(GTK_BOX(box), zc);
        }
    }
    if (d != NULL && d->gps_track != NULL && d->gps_track->len >= 2) {
        GtkWidget *sub = NULL;
        GtkWidget *card = mi_card_new(&inner, 10);
        gtk_box_append(GTK_BOX(inner), mi_card_header("Route", &sub, NULL));
        g_snprintf(buf, sizeof(buf), "%u GPS points · green = start, red = finish", d->gps_track->len);
        gtk_label_set_text(GTK_LABEL(sub), buf);
        GtkWidget *area = gtk_drawing_area_new();
        gtk_widget_set_size_request(area, -1, 260);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), draw_detail_route, dash, NULL);
        gtk_box_append(GTK_BOX(inner), area);
        gtk_box_append(GTK_BOX(box), card);
    }

    GtkWidget *clamp = adw_clamp_new();
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp), 960);
    adw_clamp_set_tightening_threshold(ADW_CLAMP(clamp), 600);
    adw_clamp_set_child(ADW_CLAMP(clamp), box);
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), clamp);

    GtkWidget *tb = adw_toolbar_view_new();
    GtkWidget *header = adw_header_bar_new();
    GtkWidget *title = adw_window_title_new(e->label != NULL ? e->label : "Workout", NULL);
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), title);
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(tb), header);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(tb), scroll);
    g_free(when);

    AdwNavigationPage *page = adw_navigation_page_new(tb, e->label != NULL ? e->label : "Workout");
    g_signal_connect(page, "hidden", G_CALLBACK(on_detail_hidden), dash);
    dash->sport_detail_page = GTK_WIDGET(page);
    adw_navigation_view_push(ADW_NAVIGATION_VIEW(dash->nav_view), page);
}

/* ======================================================================================== */
/* List page                                                                                */
/* ======================================================================================== */

static void on_row_activated(AdwActionRow *row, gpointer user_data) {
    AppState *state = user_data;
    MiwearSportEntry *e = g_object_get_data(G_OBJECT(row), "mi-entry");
    if (e != NULL && g_list_find(state->sport_entries, e) != NULL) {
        open_detail(state, e);
    }
}

void miwear_sport_rebuild_list(AppState *state) {
    MiwearDashboardUi *dash = state != NULL ? state->dash_ui : NULL;
    if (dash == NULL || dash->sport_list_box == NULL) {
        return;
    }
    mi_clear_box(dash->sport_list_box);
    GList *sorted = g_list_sort(g_list_copy(state->sport_entries), sport_entry_time_cmp);
    const gboolean empty = sorted == NULL;
    gtk_widget_set_visible(dash->sport_empty, empty);
    gtk_widget_set_visible(dash->sport_summary, !empty);

    /* Last 30 days summary */
    const gint64 now = g_get_real_time() / G_USEC_PER_SEC;
    int count = 0, dist = 0, dur = 0, cal = 0;
    for (GList *l = sorted; l != NULL; l = l->next) {
        const MiwearSportEntry *e = l->data;
        if ((gint64)entry_start(e) >= now - 30 * 86400) {
            count++;
            dist += MAX(e->distance_m, 0);
            dur += MAX(e->duration_sec, 0);
            cal += MAX(e->calories, 0);
        }
    }
    GtkWidget **values = g_object_get_data(G_OBJECT(dash->sport_summary), "mi-values");
    gchar buf[32];
    g_snprintf(buf, sizeof(buf), "%d", count);
    gtk_label_set_text(GTK_LABEL(values[0]), buf);
    mi_format_minutes(dur / 60, buf, sizeof(buf));
    gtk_label_set_text(GTK_LABEL(values[1]), buf);
    format_distance(dist, buf, sizeof(buf));
    gtk_label_set_text(GTK_LABEL(values[2]), dist > 0 ? buf : "—");
    g_snprintf(buf, sizeof(buf), "%d kcal", cal);
    gtk_label_set_text(GTK_LABEL(values[3]), cal > 0 ? buf : "—");

    GtkWidget *group = NULL;
    gchar *group_key = NULL;
    for (GList *l = sorted; l != NULL; l = l->next) {
        MiwearSportEntry *e = l->data;
        GDateTime *dt = g_date_time_new_from_unix_local((gint64)entry_start(e));
        gchar *month = g_date_time_format(dt, "%B %Y");
        if (group == NULL || g_strcmp0(month, group_key) != 0) {
            group = adw_preferences_group_new();
            adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(group), month);
            gtk_box_append(GTK_BOX(dash->sport_list_box), group);
            g_free(group_key);
            group_key = g_strdup(month);
        }
        gchar *when = g_date_time_format(dt, "%a %-d %b · %H:%M");
        g_date_time_unref(dt);
        g_free(month);

        GtkWidget *row = adw_action_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), e->label != NULL ? e->label : "Workout");
        GString *sub = g_string_new(when);
        if (e->duration_sec > 0) {
            g_string_append_printf(sub, " · %d min", (e->duration_sec + 59) / 60);
        }
        if (e->hr_avg > 0) {
            g_string_append_printf(sub, " · ♥ %d", e->hr_avg);
        }
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), sub->str);
        g_string_free(sub, TRUE);
        g_free(when);
        GtkWidget *icon = mi_icon(sport_icon(e), MI_COLOR_STEPS, 18);
        gtk_widget_add_css_class(icon, "mi-bubble");
        gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
        adw_action_row_add_prefix(ADW_ACTION_ROW(row), icon);
        gchar dist_txt[32];
        if (e->distance_m > 0) {
            format_distance(e->distance_m, dist_txt, sizeof(dist_txt));
        } else if (e->calories > 0) {
            g_snprintf(dist_txt, sizeof(dist_txt), "%d kcal", e->calories);
        } else {
            dist_txt[0] = '\0';
        }
        GtkWidget *suffix = mi_label(dist_txt, "heading numeric", 1.0f);
        adw_action_row_add_suffix(ADW_ACTION_ROW(row), suffix);
        adw_action_row_add_suffix(ADW_ACTION_ROW(row), gtk_image_new_from_icon_name("go-next-symbolic"));
        gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), TRUE);
        g_object_set_data(G_OBJECT(row), "mi-entry", e);
        g_signal_connect(row, "activated", G_CALLBACK(on_row_activated), state);
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), row);
    }
    g_free(group_key);
    g_list_free(sorted);
}

GtkWidget *mi_workouts_page_new(AppState *state, MiwearDashboardUi *dash) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
    gtk_widget_add_css_class(box, "mi-page");

    GtkWidget *inner = NULL;
    dash->sport_summary = mi_card_new(&inner, 12);
    gtk_widget_set_visible(dash->sport_summary, FALSE);
    gtk_box_append(GTK_BOX(inner), mi_label("Last 30 Days", "heading", 0.0f));
    GtkWidget *strip = mi_stat_flow_new();
    GtkWidget **vals = g_new0(GtkWidget *, 4);
    mi_stat_flow_append(strip, mi_stat_new("Workouts", &vals[0]));
    mi_stat_flow_append(strip, mi_stat_new("Active time", &vals[1]));
    mi_stat_flow_append(strip, mi_stat_new("Distance", &vals[2]));
    mi_stat_flow_append(strip, mi_stat_new("Calories", &vals[3]));
    gtk_box_append(GTK_BOX(inner), strip);
    g_object_set_data_full(G_OBJECT(dash->sport_summary), "mi-values", vals, g_free);
    gtk_box_append(GTK_BOX(box), dash->sport_summary);

    dash->sport_empty = adw_status_page_new();
    adw_status_page_set_icon_name(ADW_STATUS_PAGE(dash->sport_empty), "miwear-run-symbolic");
    adw_status_page_set_title(ADW_STATUS_PAGE(dash->sport_empty), "No Workouts Yet");
    adw_status_page_set_description(ADW_STATUS_PAGE(dash->sport_empty),
                                    "Workouts recorded on the band show up here after a sync.");
    gtk_box_append(GTK_BOX(box), dash->sport_empty);

    dash->sport_list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
    gtk_box_append(GTK_BOX(box), dash->sport_list_box);

    GtkWidget *clamp = adw_clamp_new();
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp), 760);
    adw_clamp_set_child(ADW_CLAMP(clamp), box);
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), clamp);
    miwear_sport_rebuild_list(state);
    return scroll;
}
