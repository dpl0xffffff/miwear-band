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
    const int t = e->detail != NULL && e->detail->sub_sport_type > 0 ? e->detail->sub_sport_type : e->sport_subtype;
    switch (t) {
        case 1: case 3: case 5: case 208: return "miwear-run-symbolic";
        case 2: case 4: case 15: case 22: case 207: case 333: return "miwear-walk-symbolic";
        case 6: case 7: case 23: case 206: case 324: return "miwear-bike-symbolic";
        case 9: case 10: case 25: case 109: case 111: case 112: case 116: case 117: case 118:
            return "miwear-swim-symbolic";
        default: break;
    }
    const char *l = e->label != NULL ? e->label : "";
    gchar *low = g_ascii_strdown(l, -1);
    const char *icon = "miwear-dumbbell-symbolic";
    if (strstr(low, "swim") != NULL) {
        icon = "miwear-swim-symbolic";
    } else if (strstr(low, "cycl") != NULL || strstr(low, "bik") != NULL || strstr(low, "ride") != NULL) {
        icon = "miwear-bike-symbolic";
    } else if (strstr(low, "walk") != NULL || strstr(low, "hik") != NULL) {
        icon = "miwear-walk-symbolic";
    } else if (strstr(low, "run") != NULL || strstr(low, "treadmill") != NULL) {
        icon = "miwear-run-symbolic";
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
/* Training load and VO2 max                                                                */
/* ======================================================================================== */

static const char *const load_level_names[5] = {"", "Too light", "Optimal", "High", "Overreaching"};
static const MiColor load_level_colors[4] = {MI_COLOR_SPO2, MI_COLOR_STEPS, MI_COLOR_STRESS, MI_COLOR_HR};
static const char *const vo2_level_names[8] = {"", "Novice", "Beginner", "Fair", "Average", "Good", "Excellent",
                                               "Elite"};
static const MiColor vo2_level_colors[7] = {MI_COLOR_HR,     MI_COLOR_CALORIES, MI_COLOR_STRESS, MI_COLOR_STEPS,
                                            MI_COLOR_ACTIVE, MI_COLOR_SPO2,     MI_COLOR_SLEEP};
static const char *const training_status_names[9] = {"",         "No status", "Overreaching", "Detraining",
                                                     "Unproductive", "Maintaining", "Recovery", "Productive",
                                                     "Peaking"};

static int entry_training_load(const MiwearWorkoutSummary *d) {
    if (d == NULL) {
        return 0;
    }
    return d->training_load > 0 ? d->training_load : d->walk_training_load > 0 ? d->walk_training_load
                                                                                 : d->swim_training_load;
}

static int entry_recovery_min(const MiwearWorkoutSummary *d) {
    if (d == NULL) {
        return 0;
    }
    return d->recovery_min > 0 ? d->recovery_min : MAX(d->walk_recovery_min, d->swim_recovery_min);
}

/* Segmented level gauge: `count` coloured segments, the active one solid with its name under it. */
typedef struct {
    int count;
    int active; /* 1-based, 0 = none */
    const MiColor *colors;
    const char *const *names; /* 1-based */
} LevelGauge;

static void draw_level_gauge(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
    (void)height;
    const LevelGauge *g = user_data;
    GtkWidget *w = GTK_WIDGET(area);
    const double gap = 4, bar_h = 10, top = 6;
    const double seg = (width - gap * (g->count - 1)) / g->count;
    for (int i = 0; i < g->count; i++) {
        const double x = i * (seg + gap);
        mi_cairo_set_color(cr, g->colors[i], i + 1 == g->active ? 1.0 : 0.22);
        mi_rounded_rect(cr, x, top, seg, bar_h, bar_h / 2);
        cairo_fill(cr);
        if (i + 1 == g->active) {
            const double cx = x + seg / 2;
            mi_cairo_set_fg(cr, w, 0.85);
            cairo_move_to(cr, cx - 5, 0);
            cairo_line_to(cr, cx + 5, 0);
            cairo_line_to(cr, cx, top - 1);
            cairo_close_path(cr);
            cairo_fill(cr);
        }
    }
    /* End labels plus the active level name. */
    mi_draw_text(cr, w, g->names[1], 0, top + bar_h + 15, 10.5, g->active == 1 ? 0.9 : 0.5, 0.0);
    mi_draw_text(cr, w, g->names[g->count], width, top + bar_h + 15, 10.5, g->active == g->count ? 0.9 : 0.5, 1.0);
    if (g->active > 1 && g->active < g->count) {
        const double cx = (g->active - 1) * (seg + gap) + seg / 2;
        mi_draw_text(cr, w, g->names[g->active], cx, top + bar_h + 15, 10.5, 0.9, 0.5);
    }
}

static GtkWidget *level_gauge_new(int count, int active, const MiColor *colors, const char *const *names) {
    LevelGauge *g = g_new(LevelGauge, 1);
    *g = (LevelGauge){count, CLAMP(active, 0, count), colors, names};
    GtkWidget *area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area, -1, 36);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), draw_level_gauge, g, g_free);
    return area;
}

/* Big value + coloured level text, as a header row of a training card. */
static GtkWidget *training_value_row(const char *value, const char *unit, const char *level, MiColor color) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *v = mi_label(value, "mi-tile-value numeric", 0.0f);
    gtk_widget_set_valign(v, GTK_ALIGN_BASELINE_FILL);
    gtk_box_append(GTK_BOX(row), v);
    if (unit != NULL) {
        GtkWidget *u = mi_label(unit, "mi-tile-unit", 0.0f);
        gtk_widget_set_valign(u, GTK_ALIGN_BASELINE_FILL);
        gtk_box_append(GTK_BOX(row), u);
    }
    if (level != NULL && level[0] != '\0') {
        GtkWidget *l = mi_label(level, "heading", 1.0f);
        gtk_widget_add_css_class(l, mi_color_css_class(color));
        gtk_widget_set_hexpand(l, TRUE);
        gtk_widget_set_valign(l, GTK_ALIGN_BASELINE_FILL);
        gtk_box_append(GTK_BOX(row), l);
    }
    return row;
}

/* "Training" card on a workout's detail page; NULL when the band reported none of it. */
static GtkWidget *workout_training_card(const MiwearWorkoutSummary *d) {
    const int load = entry_training_load(d);
    const int recovery = entry_recovery_min(d);
    const float te_aer = d->has_swim_detail ? d->swim_training_effect : d->walk_te_aerobic;
    const float te_ana = d->walk_te_anaerobic;
    if (load <= 0 && d->vo2_max <= 0 && recovery <= 0 && te_aer <= 0.05f && d->training_status <= 0) {
        return NULL;
    }
    GtkWidget *inner = NULL;
    GtkWidget *card = mi_card_new(&inner, 12);
    gchar buf[64];

    if (load > 0) {
        gtk_box_append(GTK_BOX(inner), mi_card_header("Training Load", NULL, NULL));
        const int lvl = CLAMP(d->training_load_level, 0, 4);
        g_snprintf(buf, sizeof(buf), "%d", load);
        gtk_box_append(GTK_BOX(inner), training_value_row(buf, NULL, load_level_names[lvl],
                                                          lvl > 0 ? load_level_colors[lvl - 1] : MI_COLOR_STEPS));
        if (lvl > 0) {
            gtk_box_append(GTK_BOX(inner), level_gauge_new(4, lvl, load_level_colors, load_level_names));
        }
    }
    if (d->vo2_max > 0) {
        GtkWidget *hdr = mi_card_header("VO₂ Max", NULL, NULL);
        if (load > 0) {
            gtk_widget_set_margin_top(hdr, 8);
        }
        gtk_box_append(GTK_BOX(inner), hdr);
        const int lvl = CLAMP(d->vo2_max_level, 0, 7);
        g_snprintf(buf, sizeof(buf), "%d", d->vo2_max);
        gtk_box_append(GTK_BOX(inner), training_value_row(buf, "ml/kg/min", vo2_level_names[lvl],
                                                          lvl > 0 ? vo2_level_colors[lvl - 1] : MI_COLOR_SPO2));
        if (lvl > 0) {
            gtk_box_append(GTK_BOX(inner), level_gauge_new(7, lvl, vo2_level_colors, vo2_level_names));
        }
    }

    GtkWidget *stats = mi_stat_flow_new();
    GtkWidget *v;
    if (d->training_status > 0 && d->training_status <= 8) {
        mi_stat_flow_append(stats, mi_stat_new("Training status", &v));
        gtk_label_set_text(GTK_LABEL(v), training_status_names[d->training_status]);
    }
    if (te_aer > 0.05f) {
        mi_stat_flow_append(stats, mi_stat_new("Aerobic effect", &v));
        g_snprintf(buf, sizeof(buf), "%.1f", (double)te_aer);
        gtk_label_set_text(GTK_LABEL(v), buf);
    }
    if (te_ana > 0.05f) {
        mi_stat_flow_append(stats, mi_stat_new("Anaerobic effect", &v));
        g_snprintf(buf, sizeof(buf), "%.1f", (double)te_ana);
        gtk_label_set_text(GTK_LABEL(v), buf);
    }
    if (recovery > 0) {
        mi_stat_flow_append(stats, mi_stat_new("Recovery time", &v));
        miwear_format_recovery_duration(recovery, buf, sizeof(buf));
        gtk_label_set_text(GTK_LABEL(v), buf);
    }
    if (gtk_widget_get_first_child(stats) != NULL) {
        gtk_widget_set_margin_top(stats, 4);
        gtk_box_append(GTK_BOX(inner), stats);
    }
    return card;
}

/* ---- Workouts page summary cards --------------------------------------------------------- */

static int training_week_day_load[7];

static void draw_week_load(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
    (void)user_data;
    GtkWidget *w = GTK_WIDGET(area);
    int max = 1;
    for (int i = 0; i < 7; i++) {
        max = MAX(max, training_week_day_load[i]);
    }
    const double bottom = 16, slot = width / 7.0, bw = MIN(slot * 0.5, 22);
    GDateTime *now = g_date_time_new_now_local();
    for (int i = 0; i < 7; i++) {
        const double cx = slot * (i + 0.5);
        const int v = training_week_day_load[i];
        const double bh = v > 0 ? MAX(3.0, (height - bottom - 4) * v / max) : 3.0;
        mi_cairo_set_color(cr, MI_COLOR_STEPS, v > 0 ? (i == 6 ? 1.0 : 0.6) : 0.18);
        mi_rounded_rect(cr, cx - bw / 2, height - bottom - bh, bw, bh, MIN(4, bw / 2));
        cairo_fill(cr);
        GDateTime *d = g_date_time_add_days(now, i - 6);
        gchar *wd = g_date_time_format(d, "%a");
        g_date_time_unref(d);
        mi_draw_text(cr, w, wd, cx, height - 3, 10, i == 6 ? 0.85 : 0.5, 0.5);
        g_free(wd);
    }
    g_date_time_unref(now);
}

/* Fills the two summary cards from the band's daily summaries and the stored workouts. */
static void refresh_training_cards(AppState *state) {
    MiwearDashboardUi *dash = state->dash_ui;
    GtkWidget *load_box = g_object_get_data(G_OBJECT(dash->sport_list_box), "mi-load-box");
    GtkWidget *vo2_box = g_object_get_data(G_OBJECT(dash->sport_list_box), "mi-vo2-box");
    if (load_box == NULL || vo2_box == NULL) {
        return;
    }
    mi_clear_box(load_box);
    mi_clear_box(vo2_box);
    gchar buf[96];

    /* Training load: latest daily summary that carries it (today's, when synced). */
    MiwearDailySummaryRow latest = {0};
    gboolean have_latest = FALSE;
    GDateTime *now = g_date_time_new_now_local();
    g_mutex_lock(&state->recorded_lock);
    for (int i = 0; i < 7; i++) {
        GDateTime *d = g_date_time_add_days(now, i - 6);
        gchar *key = g_date_time_format(d, "%Y-%m-%d");
        g_date_time_unref(d);
        const MiwearDailySummaryRow *r =
            state->daily_summaries != NULL ? g_hash_table_lookup(state->daily_summaries, key) : NULL;
        g_free(key);
        training_week_day_load[i] = (r != NULL && r->has_training_vitality) ? r->training_load_day : 0;
        if (r != NULL && r->has_training_vitality) {
            latest = *r;
            have_latest = TRUE;
        }
    }
    g_mutex_unlock(&state->recorded_lock);
    g_date_time_unref(now);

    gtk_box_append(GTK_BOX(load_box), mi_card_header("Training Load", NULL, NULL));
    if (have_latest) {
        const int lvl = CLAMP(latest.training_load_level, 0, 4);
        g_snprintf(buf, sizeof(buf), "%d", latest.training_load_week);
        gtk_box_append(GTK_BOX(load_box), training_value_row(buf, "7 days", load_level_names[lvl],
                                                             lvl > 0 ? load_level_colors[lvl - 1] : MI_COLOR_STEPS));
        if (lvl > 0) {
            gtk_box_append(GTK_BOX(load_box), level_gauge_new(4, lvl, load_level_colors, load_level_names));
        }
        GtkWidget *bars = gtk_drawing_area_new();
        gtk_widget_set_size_request(bars, -1, 64);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(bars), draw_week_load, NULL, NULL);
        gtk_box_append(GTK_BOX(load_box), bars);
        g_snprintf(buf, sizeof(buf), "Today %d · from the band's daily summary", latest.training_load_day);
        gtk_box_append(GTK_BOX(load_box), mi_label(buf, "caption dimmed", 0.0f));
    } else {
        GtkWidget *l = mi_label("Comes with the band's daily summary after a sync.", "dimmed", 0.0f);
        gtk_label_set_wrap(GTK_LABEL(l), TRUE);
        gtk_box_append(GTK_BOX(load_box), l);
    }

    /* VO2 max: latest workout that estimated it. */
    const MiwearSportEntry *best = NULL;
    for (GList *l = state->sport_entries; l != NULL; l = l->next) {
        const MiwearSportEntry *e = l->data;
        if (e->detail != NULL && e->detail->vo2_max > 0 && (best == NULL || entry_start(e) > entry_start(best))) {
            best = e;
        }
    }
    gtk_box_append(GTK_BOX(vo2_box), mi_card_header("VO₂ Max", NULL, NULL));
    if (best != NULL) {
        const MiwearWorkoutSummary *d = best->detail;
        const int lvl = CLAMP(d->vo2_max_level, 0, 7);
        g_snprintf(buf, sizeof(buf), "%d", d->vo2_max);
        gtk_box_append(GTK_BOX(vo2_box), training_value_row(buf, "ml/kg/min", vo2_level_names[lvl],
                                                            lvl > 0 ? vo2_level_colors[lvl - 1] : MI_COLOR_SPO2));
        if (lvl > 0) {
            gtk_box_append(GTK_BOX(vo2_box), level_gauge_new(7, lvl, vo2_level_colors, vo2_level_names));
        }
        GDateTime *dt = g_date_time_new_from_unix_local((gint64)entry_start(best));
        gchar *when = g_date_time_format(dt, "%-d %b");
        g_date_time_unref(dt);
        g_snprintf(buf, sizeof(buf), "From %s on %s", best->label != NULL ? best->label : "a workout", when);
        g_free(when);
        gtk_box_append(GTK_BOX(vo2_box), mi_label(buf, "caption dimmed", 0.0f));
    } else {
        GtkWidget *l = mi_label("The band estimates VO₂ max during outdoor runs and walks with heart rate. "
                                "It shows up here after the next one is synced.",
                                "dimmed", 0.0f);
        gtk_label_set_wrap(GTK_LABEL(l), TRUE);
        gtk_box_append(GTK_BOX(vo2_box), l);
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

#define DETAIL_BINS_MAX 240
#define DETAIL_STEPS (-1)
#define DETAIL_HR (-2)
static guint detail_bins(const MiwearWorkoutSummary *d, int series, guint nbins, double *out, uint32_t *t_end);

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

    /* Bucket averages (about 2 px each), coloured by heart-rate zone, broken where nothing was
     * recorded (pauses). */
    double bins[DETAIL_BINS_MAX];
    uint32_t t_bins = 1;
    const guint nb = detail_bins(d, DETAIL_HR, (guint)(pw / 2), bins, &t_bins);
    cairo_set_line_width(cr, 2.2);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    for (guint b = 1; b < nb; b++) {
        if (isnan(bins[b]) || isnan(bins[b - 1])) {
            continue;
        }
        const int z = zone_for_hr(&d->hr_zones, (int)lround(bins[b]));
        if (z >= 0) {
            mi_cairo_set_color(cr, zone_colors[z], 1.0);
        } else {
            mi_cairo_set_color(cr, MI_COLOR_HR, 1.0);
        }
        cairo_move_to(cr, left + pw * (b - 0.5) / nb, DY(bins[b - 1]));
        cairo_line_to(cr, left + pw * (b + 0.5) / nb, DY(bins[b]));
        cairo_stroke(cr);
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

/* Time buckets over the record: steps are summed, other series averaged (NAN = no data). */
static guint detail_bins(const MiwearWorkoutSummary *d, int series, guint nbins, double *out, uint32_t *t_end) {
    const guint n = d->record_samples->len;
    *t_end = MAX(g_array_index(d->record_samples, MiwearWorkoutSample, n - 1).offset_sec, 1u);
    nbins = CLAMP(nbins, 1u, DETAIL_BINS_MAX);
    int cnt[DETAIL_BINS_MAX] = {0};
    for (guint b = 0; b < nbins; b++) {
        out[b] = 0;
    }
    for (guint i = 0; i < n; i++) {
        const MiwearWorkoutSample *smp = &g_array_index(d->record_samples, MiwearWorkoutSample, i);
        const guint b = MIN(nbins - 1, (guint)((double)smp->offset_sec / *t_end * nbins));
        const double v = series == DETAIL_STEPS ? smp->steps_delta
                         : series == DETAIL_HR   ? (smp->hr > 0 ? smp->hr : NAN)
                                                 : smp->v[series];
        if (!isnan(v)) {
            out[b] += v;
            cnt[b]++;
        }
    }
    for (guint b = 0; b < nbins; b++) {
        if (series != DETAIL_STEPS) {
            out[b] = cnt[b] > 0 ? out[b] / cnt[b] : NAN;
        }
    }
    return nbins;
}

static void draw_time_axis(cairo_t *cr, GtkWidget *w, double left, double pw, double y, uint32_t t_end) {
    const int minutes = (int)(t_end / 60);
    const int step = minutes > 90 ? 30 : minutes > 40 ? 15 : minutes > 15 ? 5 : 1;
    for (int m = 0; m <= minutes; m += step) {
        gchar t[16];
        g_snprintf(t, sizeof(t), "%d′", m);
        mi_draw_text(cr, w, t, CLAMP(left + pw * m * 60.0 / t_end, left + 8, left + pw - 8), y, 10.5, 0.55, 0.5);
    }
}

static void draw_detail_steps(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
    MiwearDashboardUi *dash = user_data;
    GtkWidget *w = GTK_WIDGET(area);
    const MiwearWorkoutSummary *d = entry_detail(dash->sport_detail_entry);
    if (d == NULL || d->record_samples == NULL || d->record_samples->len == 0 || width < 80) {
        return;
    }
    const double left = 34, top = 6, bottom = 22;
    const double pw = width - left, ph = height - top - bottom;
    double bins[DETAIL_BINS_MAX];
    uint32_t t_end = 1;
    const guint nb = detail_bins(d, DETAIL_STEPS, (guint)(pw / 6), bins, &t_end);
    double smax = 1;
    for (guint b = 0; b < nb; b++) {
        smax = MAX(smax, bins[b]);
    }
    gchar t[16];
    g_snprintf(t, sizeof(t), "%.0f", smax);
    mi_draw_text(cr, w, t, left - 6, top + 8, 10.5, 0.55, 1.0);
    mi_draw_text(cr, w, "0", left - 6, top + ph, 10.5, 0.55, 1.0);
    draw_time_axis(cr, w, left, pw, top + ph + 16, t_end);
    const double bw = pw / nb;
    mi_cairo_set_color(cr, MI_COLOR_STEPS, 0.9);
    for (guint b = 0; b < nb; b++) {
        if (bins[b] <= 0) {
            continue;
        }
        const double bh = ph * bins[b] / smax;
        mi_rounded_rect(cr, left + bw * b + bw * 0.1, top + ph - bh, MAX(bw * 0.8, 1), bh, MIN(2.5, bw * 0.4));
        cairo_fill(cr);
    }
}

static MiColor series_color(int k) {
    switch (k) {
        case MIWEAR_SERIES_PACE:
        case MIWEAR_SERIES_SPEED:
            return MI_COLOR_ACTIVE;
        case MIWEAR_SERIES_CADENCE:
        case MIWEAR_SERIES_STRIDE:
            return MI_COLOR_STEPS;
        case MIWEAR_SERIES_POWER:
            return MI_COLOR_CALORIES;
        case MIWEAR_SERIES_ALTITUDE:
            return MI_COLOR_VITALITY;
        case MIWEAR_SERIES_SPO2:
            return MI_COLOR_SPO2;
        case MIWEAR_SERIES_STRESS:
            return MI_COLOR_STRESS;
        case MIWEAR_SERIES_WATER_TEMP:
            return MI_COLOR_HR;
        case MIWEAR_SERIES_GROUND_CONTACT:
        case MIWEAR_SERIES_VERTICAL_OSC:
            return MI_COLOR_CYCLES;
        default:
            return MI_COLOR_SLEEP;
    }
}

static void format_series_value(int k, double v, gchar *buf, gsize len) {
    if (k == MIWEAR_SERIES_PACE) {
        const int sec = (int)lround(v);
        g_snprintf(buf, len, "%d′%02d″", sec / 60, sec % 60);
    } else if (k == MIWEAR_SERIES_SPEED || k == MIWEAR_SERIES_ALTITUDE || k == MIWEAR_SERIES_DEPTH ||
               k == MIWEAR_SERIES_WATER_TEMP || k == MIWEAR_SERIES_VERTICAL_OSC) {
        g_snprintf(buf, len, "%.1f", v);
    } else {
        g_snprintf(buf, len, "%.0f", v);
    }
}

static void draw_detail_series(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
    MiwearDashboardUi *dash = user_data;
    GtkWidget *w = GTK_WIDGET(area);
    const int k = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(area), "series"));
    const MiwearWorkoutSummary *d = entry_detail(dash->sport_detail_entry);
    if (d == NULL || d->record_samples == NULL || d->record_samples->len < 2 || width < 80) {
        return;
    }
    const double left = 44, right = 6, top = 8, bottom = 22;
    const double pw = width - left - right, ph = height - top - bottom;
    double bins[DETAIL_BINS_MAX];
    uint32_t t_end = 1;
    const guint nb = detail_bins(d, k, (guint)(pw / 3), bins, &t_end);
    double lo = INFINITY, hi = -INFINITY;
    for (guint b = 0; b < nb; b++) {
        if (!isnan(bins[b])) {
            lo = MIN(lo, bins[b]);
            hi = MAX(hi, bins[b]);
        }
    }
    if (!isfinite(lo)) {
        return;
    }
    const double pad = MAX((hi - lo) * 0.1, k == MIWEAR_SERIES_PACE ? 10 : 1);
    lo -= pad;
    hi += pad;
    const gboolean invert = k == MIWEAR_SERIES_PACE; /* faster (lower) pace up, as Mi Fitness does */
#define SX(b) (left + pw * ((b) + 0.5) / nb)
#define SY(v) (top + ph * (invert ? ((v) - lo) / (hi - lo) : 1.0 - ((v) - lo) / (hi - lo)))
    for (int g = 0; g <= 3; g++) {
        const double v = lo + (hi - lo) * g / 3;
        mi_cairo_set_fg(cr, w, g == (invert ? 3 : 0) ? 0.18 : 0.07);
        cairo_set_line_width(cr, 1);
        cairo_move_to(cr, left, round(SY(v)) + 0.5);
        cairo_line_to(cr, left + pw, round(SY(v)) + 0.5);
        cairo_stroke(cr);
        gchar t[24];
        format_series_value(k, v, t, sizeof(t));
        mi_draw_text(cr, w, t, left - 6, SY(v) + 3.5, 10.5, 0.55, 1.0);
    }
    draw_time_axis(cr, w, left, pw, top + ph + 16, t_end);

    /* Filled runs between gaps, then the line on top. */
    const double base = top + ph;
    for (int pass = 0; pass < 2; pass++) {
        gboolean open = FALSE;
        double first_x = 0, last_x = 0;
        for (guint b = 0; b <= nb; b++) {
            const gboolean have = b < nb && !isnan(bins[b]);
            if (have) {
                if (!open) {
                    cairo_move_to(cr, SX(b), SY(bins[b]));
                    first_x = SX(b);
                    open = TRUE;
                } else {
                    cairo_line_to(cr, SX(b), SY(bins[b]));
                }
                last_x = SX(b);
            } else if (open) {
                if (pass == 0) {
                    cairo_line_to(cr, last_x, base);
                    cairo_line_to(cr, first_x, base);
                    cairo_close_path(cr);
                    mi_cairo_set_color(cr, series_color(k), 0.14);
                    cairo_fill(cr);
                } else {
                    mi_cairo_set_color(cr, series_color(k), 1.0);
                    cairo_set_line_width(cr, 2.0);
                    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
                    cairo_stroke(cr);
                }
                open = FALSE;
            }
        }
    }
#undef SX
#undef SY
}

/* avg / max (min for pace) of a series, FALSE when it was never recorded. */
static gboolean series_stats(const MiwearWorkoutSummary *d, int k, double *avg, double *best) {
    double sum = 0;
    guint n = 0;
    *best = k == MIWEAR_SERIES_PACE ? INFINITY : -INFINITY;
    for (guint i = 0; i < d->record_samples->len; i++) {
        const double v = g_array_index(d->record_samples, MiwearWorkoutSample, i).v[k];
        if (!isnan(v)) {
            sum += v;
            n++;
            *best = k == MIWEAR_SERIES_PACE ? MIN(*best, v) : MAX(*best, v);
        }
    }
    *avg = n > 0 ? sum / n : 0;
    return n > 1;
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

/* ---- Per-sport metrics (Mi Fitness report field ids; units from the Mi Fitness schemas) ---- */

typedef enum {
    MF_COUNT, MF_SEC, MF_KCAL, MF_BPM, MF_PACE, MF_KMH, MF_M1, MF_M, MF_CM, MF_SPM, MF_PER_MIN, MF_RPM,
    MF_WATT, MF_PCT, MF_DEG, MF_MS, MF_TENTH_BW, MF_FLOAT1, MF_PLUS, MF_STROKE, MF_LANDING, MF_LEVEL,
} MetricFmt;

typedef struct {
    int id;
    const char *label;
    MetricFmt fmt;
} MetricDef;

/* In display order. Fields shown elsewhere (duration, distance, calories, average heart rate,
 * training load/VO2 max/effects/recovery, heart-rate zones) are left out. */
static const MetricDef metric_defs[] = {
    {17, "Max heart rate", MF_BPM}, {18, "Min heart rate", MF_BPM}, {4, "Active time", MF_SEC},
    {6, "Active calories", MF_KCAL}, {10, "Avg pace", MF_PACE}, {8, "Best pace", MF_PACE},
    {11, "Avg speed", MF_KMH}, {12, "Max speed", MF_KMH}, {13, "Steps", MF_COUNT},
    {15, "Avg cadence", MF_SPM}, {14, "Max cadence", MF_SPM}, {148, "Avg stride", MF_CM},
    {19, "Total ascent", MF_M1}, {20, "Total descent", MF_M1}, {22, "Max altitude", MF_M1},
    {23, "Min altitude", MF_M1}, {74, "Max slope", MF_DEG}, {115, "Avg cadence", MF_RPM},
    {116, "Max cadence", MF_RPM}, {82, "Avg power", MF_WATT}, {83, "Max power", MF_WATT},
    {84, "Avg resistance", MF_COUNT}, {85, "Max resistance", MF_COUNT}, {173, "Avg running power", MF_WATT},
    {174, "Max running power", MF_WATT}, {39, "Lengths", MF_COUNT}, {42, "Pool length", MF_M},
    {36, "Main stroke", MF_STROKE}, {35, "Strokes", MF_COUNT}, {157, "Avg stroke rate", MF_PER_MIN},
    {40, "Avg SWOLF", MF_COUNT}, {41, "Best SWOLF", MF_COUNT}, {43, "Strokes", MF_COUNT},
    {44, "Avg stroke rate", MF_PER_MIN}, {45, "Best stroke rate", MF_PER_MIN}, {46, "Drive time", MF_SEC},
    {47, "Recovery time", MF_SEC}, {48, "Jumps", MF_COUNT}, {49, "Avg jump rate", MF_PER_MIN},
    {50, "Best jump rate", MF_PER_MIN}, {56, "Trips", MF_COUNT}, {64, "Sets", MF_COUNT},
    {63, "Rest between sets", MF_SEC}, {58, "Swings", MF_COUNT}, {59, "Forehands", MF_COUNT},
    {60, "Backhands", MF_COUNT}, {61, "Smashes", MF_COUNT}, {62, "Serves", MF_COUNT}, {75, "Strokes", MF_COUNT},
    {80, "Shots", MF_COUNT}, {81, "Dribbling time", MF_SEC}, {182, "Dive time", MF_SEC},
    {189, "Dives", MF_COUNT}, {101, "Avg SpO₂", MF_PCT}, {104, "Avg stress", MF_COUNT},
    {118, "Punches", MF_COUNT}, {123, "Best combo", MF_COUNT}, {124, "Hit rate", MF_PCT},
    {220, "Reps", MF_COUNT}, {222, "Sets", MF_COUNT}, {224, "Avg rate", MF_PER_MIN},
    {137, "Landing", MF_LANDING}, {140, "Avg impact", MF_TENTH_BW}, {142, "Avg ground contact", MF_MS},
    {144, "Avg flight time", MF_MS}, {112, "Running index", MF_FLOAT1}, {163, "Peak training effect", MF_FLOAT1},
    {165, "Training stress", MF_COUNT}, {28, "Energy used", MF_LEVEL}, {117, "Vitality", MF_PLUS},
};

static void format_metric(MetricFmt fmt, double v, int sport, gchar *buf, gsize len) {
    const int n = (int)(v + (v >= 0 ? 0.5 : -0.5));
    switch (fmt) {
        case MF_SEC: format_duration(n, buf, len); break;
        case MF_KCAL: g_snprintf(buf, len, "%d kcal", n); break;
        case MF_BPM: g_snprintf(buf, len, "%d bpm", n); break;
        case MF_PACE: {
            gchar p[32];
            miwear_format_walk_pace(n, p, sizeof(p));
            g_snprintf(buf, len, "%s %s", p, sport == 9 || sport == 10 ? "/100 m" : "/km");
            break;
        }
        case MF_KMH: g_snprintf(buf, len, "%.1f km/h", v); break;
        case MF_M1: g_snprintf(buf, len, "%.0f m", v); break;
        case MF_M: g_snprintf(buf, len, "%d m", n); break;
        case MF_CM: g_snprintf(buf, len, "%d cm", n); break;
        case MF_SPM: g_snprintf(buf, len, "%d spm", n); break;
        case MF_PER_MIN: g_snprintf(buf, len, "%d /min", n); break;
        case MF_RPM: g_snprintf(buf, len, "%d rpm", n); break;
        case MF_WATT: g_snprintf(buf, len, "%d W", n); break;
        case MF_PCT: g_snprintf(buf, len, "%d%%", n); break;
        case MF_DEG: g_snprintf(buf, len, "%d°", n); break;
        case MF_MS: g_snprintf(buf, len, "%d ms", n); break;
        case MF_TENTH_BW: g_snprintf(buf, len, "%.1f × body weight", v / 10.0); break;
        case MF_FLOAT1: g_snprintf(buf, len, "%.1f", v); break;
        case MF_PLUS: g_snprintf(buf, len, "+%d", n); break;
        case MF_LEVEL: g_snprintf(buf, len, "%d / 100", n); break;
        case MF_STROKE: g_strlcpy(buf, miwear_swim_stroke_name(n), len); break;
        case MF_LANDING: {
            static const char *k[] = {"—", "Whole foot", "Forefoot", "Heel"};
            g_strlcpy(buf, n >= 1 && n <= 3 ? k[n] : "—", len);
            break;
        }
        default: {
            gchar t[32];
            mi_format_thousands(n, t, sizeof(t));
            g_strlcpy(buf, t, len);
            break;
        }
    }
}

/* Tiles for every catalogued metric the workout reported (non-zero). TRUE when any was added. */
static gboolean add_metric_tiles(GtkWidget *flow, const MiwearWorkoutSummary *d, int sport) {
    if (d == NULL || d->metrics == NULL || d->metrics->len == 0) {
        return FALSE;
    }
    gboolean any = FALSE;
    for (size_t i = 0; i < G_N_ELEMENTS(metric_defs); i++) {
        const MetricDef *m = &metric_defs[i];
        if (!miwear_workout_has_metric(d, m->id)) {
            continue;
        }
        const double v = miwear_workout_metric(d, m->id, 0);
        if (v == 0 && m->fmt != MF_STROKE) {
            continue;
        }
        if (m->id == 6 && miwear_workout_metric(d, 7, 0) <= 0) {
            continue; /* active calories already shown as the workout's calories */
        }
        gchar buf[64];
        format_metric(m->fmt, v, sport, buf, sizeof(buf));
        add_stat_tile(flow, m->label, buf);
        any = TRUE;
    }
    return any;
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
    if (d != NULL && add_metric_tiles(flow, d, e->sport_subtype)) {
        /* Workouts decoded with the Mi Fitness tables carry every reported value. */
    } else if (d != NULL) {
        /* Older stored workouts: only the fields the previous decoder kept. */
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
            g_snprintf(t, sizeof(t), "+%d", d->walk_vitality_points);
            add_stat_tile(flow, "Vitality", d->walk_vitality_points > 0 ? t : NULL);
        }
    }
    if (gtk_widget_get_first_child(flow) != NULL) {
        gtk_box_append(GTK_BOX(box), flow);
    }
    if (d != NULL) {
        GtkWidget *tc = workout_training_card(d);
        if (tc != NULL) {
            gtk_box_append(GTK_BOX(box), tc);
        }
    }

    if (d != NULL && d->record_samples != NULL && d->record_samples->len >= 2) {
        gboolean any_hr = FALSE, any_steps = FALSE;
        for (guint i = 0; i < d->record_samples->len; i++) {
            const MiwearWorkoutSample *smp = &g_array_index(d->record_samples, MiwearWorkoutSample, i);
            any_hr |= smp->hr > 0;
            any_steps |= smp->steps_delta > 0;
        }
        GtkWidget *sub = NULL;
        GtkWidget *card = NULL;
        GtkWidget *area = NULL;
        if (any_hr) {
            card = mi_card_new(&inner, 10);
            gtk_box_append(GTK_BOX(inner), mi_card_header("Heart Rate", &sub, NULL));
            g_snprintf(buf, sizeof(buf), "avg %d · max %d · min %d bpm · dashed line = average", d->hr_avg, d->hr_max,
                       d->hr_min);
            gtk_label_set_text(GTK_LABEL(sub), buf);
            area = gtk_drawing_area_new();
            gtk_widget_set_size_request(area, -1, 190);
            gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), draw_detail_hr, dash, NULL);
            gtk_box_append(GTK_BOX(inner), area);
            gtk_box_append(GTK_BOX(box), card);
        }

        /* One chart per series the band recorded (distance and calories are per-second deltas). */
        for (int k = 0; k < MIWEAR_SERIES_COUNT; k++) {
            double avg = 0, best = 0;
            if (k == MIWEAR_SERIES_DISTANCE || k == MIWEAR_SERIES_CALORIES || !series_stats(d, k, &avg, &best)) {
                continue;
            }
            gchar a[24], m[24];
            format_series_value(k, avg, a, sizeof(a));
            format_series_value(k, best, m, sizeof(m));
            card = mi_card_new(&inner, 10);
            gtk_box_append(GTK_BOX(inner), mi_card_header(miwear_series_title(k), &sub, NULL));
            g_snprintf(buf, sizeof(buf), "avg %s · %s %s %s", a, k == MIWEAR_SERIES_PACE ? "best" : "max", m,
                       miwear_series_unit(k));
            gtk_label_set_text(GTK_LABEL(sub), buf);
            area = gtk_drawing_area_new();
            gtk_widget_set_size_request(area, -1, 140);
            g_object_set_data(G_OBJECT(area), "series", GINT_TO_POINTER(k));
            gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), draw_detail_series, dash, NULL);
            gtk_box_append(GTK_BOX(inner), area);
            gtk_box_append(GTK_BOX(box), card);
        }

        if (any_steps) {
            card = mi_card_new(&inner, 10);
            gtk_box_append(GTK_BOX(inner), mi_card_header("Steps", &sub, NULL));
            gtk_label_set_text(GTK_LABEL(sub), "Steps over the workout");
            area = gtk_drawing_area_new();
            gtk_widget_set_size_request(area, -1, 120);
            gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), draw_detail_steps, dash, NULL);
            gtk_box_append(GTK_BOX(inner), area);
            gtk_box_append(GTK_BOX(box), card);
        }
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
    refresh_training_cards(state);
}

void mi_workouts_refresh_training(AppState *state) {
    if (state != NULL && state->dash_ui != NULL && state->dash_ui->sport_list_box != NULL) {
        refresh_training_cards(state);
    }
}

GtkWidget *mi_workouts_page_new(AppState *state, MiwearDashboardUi *dash) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
    gtk_widget_add_css_class(box, "mi-page");

    /* Training load + VO2 max, side by side when there is room. */
    GtkWidget *training = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(training), GTK_SELECTION_NONE);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(training), FALSE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(training), 1);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(training), 2);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(training), 12);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(training), 12);
    GtkWidget *load_box = NULL, *vo2_box = NULL;
    GtkWidget *load_card = mi_card_new(&load_box, 10);
    GtkWidget *vo2_card = mi_card_new(&vo2_box, 10);
    gtk_widget_set_size_request(load_card, 280, -1);
    gtk_widget_set_size_request(vo2_card, 280, -1);
    mi_stat_flow_append(training, load_card);
    mi_stat_flow_append(training, vo2_card);
    gtk_box_append(GTK_BOX(box), training);

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
    g_object_set_data(G_OBJECT(dash->sport_list_box), "mi-load-box", load_box);
    g_object_set_data(G_OBJECT(dash->sport_list_box), "mi-vo2-box", vo2_box);
    gtk_box_append(GTK_BOX(box), dash->sport_list_box);

    GtkWidget *clamp = adw_clamp_new();
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp), 960);
    adw_clamp_set_child(ADW_CLAMP(clamp), box);
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), clamp);
    miwear_sport_rebuild_list(state);
    return scroll;
}
