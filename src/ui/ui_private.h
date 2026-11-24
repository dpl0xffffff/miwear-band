#ifndef MIWEAR_UI_PRIVATE_H
#define MIWEAR_UI_PRIVATE_H

/* Shared between the UI translation units (dashboard, sleep, workouts, device, widgets). */

#include "miwear/app.h"
#include "miwear/ui/dashboard.h"
#include "miwear/ui/health_data.h"

#include <adwaita.h>
#include <cairo.h>

/* ---- Palette ------------------------------------------------------------------------------ */

typedef enum {
    MI_COLOR_HR,
    MI_COLOR_SPO2,
    MI_COLOR_STRESS,
    MI_COLOR_STEPS,
    MI_COLOR_CALORIES,
    MI_COLOR_ACTIVE,
    MI_COLOR_SLEEP,
    MI_COLOR_VITALITY,
    MI_COLOR_WEIGHT,
    MI_COLOR_CYCLES,
    MI_COLOR_DEEP,
    MI_COLOR_LIGHT,
    MI_COLOR_REM,
    MI_COLOR_AWAKE,
    MI_COLOR_COUNT
} MiColor;

/* CSS class carrying the colour as `color:` (for icons and accents). */
const char *mi_color_css_class(MiColor c);
void mi_color_rgb(MiColor c, double rgb[3]);
void mi_cairo_set_color(cairo_t *cr, MiColor c, double alpha);
/* Theme foreground of a widget, with alpha. */
void mi_cairo_set_fg(cairo_t *cr, GtkWidget *widget, double alpha);
gboolean mi_is_dark(void);

void mi_style_install(GdkDisplay *display);

/* ---- Small widget helpers ----------------------------------------------------------------- */

GtkWidget *mi_label(const char *text, const char *css_class, float xalign);
GtkWidget *mi_icon(const char *icon_name, MiColor color, int pixel_size);
/* Card container (.card) with padding; returns the inner vertical box via `inner_out`. */
GtkWidget *mi_card_new(GtkWidget **inner_out, int spacing);
/* "Title" + optional dimmed subtitle header row for a card. Returns the row box. */
GtkWidget *mi_card_header(const char *title, GtkWidget **subtitle_out, GtkWidget *end_widget);
/* Stat column: big value over a caption. */
GtkWidget *mi_stat_new(const char *caption, GtkWidget **value_out);
/* Wrapping row for stat columns: 2–4 per line depending on width. */
GtkWidget *mi_stat_flow_new(void);
void mi_stat_flow_append(GtkWidget *flow, GtkWidget *child);
void mi_format_thousands(long v, gchar *buf, gsize len);
void mi_format_minutes(int minutes, gchar *buf, gsize len); /* "7h 05m" / "42 min" */
void mi_format_clock(int minute_of_day, gchar *buf, gsize len); /* "07:05" */
void mi_clear_box(GtkWidget *box);

/* ---- Drawing helpers ---------------------------------------------------------------------- */

void mi_rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r);
void mi_draw_text(cairo_t *cr, GtkWidget *widget, const char *text, double x, double y,
                  double size, double alpha, double xalign);
/* Activity rings: values/goals for up to 3 rings, outermost first. */
void mi_draw_rings(cairo_t *cr, double cx, double cy, double radius, double thickness,
                   const double *ratios, const MiColor *colors, int n);

/* ---- Per-minute day series ---------------------------------------------------------------- */

#define MI_DAY_MINUTES 1440

typedef enum {
    MI_SERIES_HR,
    MI_SERIES_SPO2,
    MI_SERIES_STRESS,
    MI_SERIES_STEPS,
    MI_SERIES_COUNT
} MiSeries;

/* Copies the series for `day_key` into values[MI_DAY_MINUTES] (-1 = none). Returns the count. */
guint mi_series_collect(AppState *state, const gchar *day_key, MiSeries series, int *values);
/* Sparkline drawing area for a series of the viewed day. */
GtkWidget *mi_sparkline_new(AppState *state, MiSeries series);

/* ---- Sleep night window ------------------------------------------------------------------- */

#define MI_NIGHT_MINUTES 1440 /* 18:00 of the previous day → 18:00 of the wake day */
#define MI_NIGHT_START_MIN (18 * 60)

typedef struct {
    int stage[MI_NIGHT_MINUTES]; /* -1 = none, else normalised stage (2 deep … 5 awake, 1 unspec) */
    int first;                   /* first index with a stage, -1 when no sleep */
    int last;
    int deep, light, rem, awake, other;
    int total_asleep;            /* band total when synced, else counted */
    gboolean has_breakdown;
    /* Per-minute sums and sample counts, used for the night's averages. */
    int hr_sum[MI_NIGHT_MINUTES], hr_n[MI_NIGHT_MINUTES];
    int spo2_sum[MI_NIGHT_MINUTES], spo2_n[MI_NIGHT_MINUTES];
    int avg_hr;   /* over first … last; 0 when no samples */
    int avg_spo2; /* over first … last; 0 when no samples */
} MiNight;

void mi_night_collect(AppState *state, GDate *wake_day, MiNight *out);

/* ---- Dashboard state ---------------------------------------------------------------------- */

typedef struct {
    GtkWidget *root;
    GtkWidget *icon;
    GtkWidget *value;
    GtkWidget *unit;
    GtkWidget *sub;
    GtkWidget *spark;
} MiTile;

struct MiwearDashboardUi {
    AppState *state;
    GtkWidget *nav_view;      /* AdwNavigationView: main page + pushed workout details */
    GtkWidget *header;
    GtkWidget *switcher_bar;
    GtkWidget *toast_overlay;
    GtkWidget *view_stack;
    GtkWidget *banner;
    GtkWidget *conn_button;
    GtkWidget *conn_icon;
    GtkWidget *sync_button;
    gboolean busy;
    gchar *status_text;
    int battery;              /* -1 unknown */

    /* Date navigation (one instance per page that shows it). */
    GPtrArray *date_titles;   /* GtkMenuButton labels */
    GPtrArray *date_next;     /* "›" buttons */
    GPtrArray *date_today;    /* "Today" buttons */
    GPtrArray *calendars;

    /* Today */
    GtkWidget *rings;
    GtkWidget *ring_steps;
    GtkWidget *ring_calories;
    GtkWidget *ring_active;
    GtkWidget *ring_badge;
    double ring_ratio[3];
    MiTile tile_hr, tile_spo2, tile_stress, tile_sleep, tile_vitality, tile_weight, tile_cycles;
    AdwNavigationPage *metric_page; /* chart of one MiSeries, pushed from a tile */
    GtkWidget *trend_area;
    GtkWidget *trend_subtitle;
    GtkWidget *trend_stat_value[4];
    GtkWidget *trend_stat_caption[4];
    int trend_metric;
    double trend_hover_x;

    /* Sleep */
    GtkWidget *sleep_total;
    GtkWidget *sleep_window;
    GtkWidget *sleep_bar;
    GtkWidget *sleep_legend;
    GtkWidget *sleep_hypnogram;
    GtkWidget *sleep_week;
    GtkWidget *sleep_content;
    GtkWidget *sleep_empty;
    MiNight *night;
    int week_minutes[7];
    double sleep_hover_x;

    /* Workouts */
    GtkWidget *sport_list_box;
    GtkWidget *sport_empty;
    GtkWidget *sport_summary;
    MiwearSportEntry *sport_detail_entry;
    GtkWidget *sport_detail_page;

    /* Device */
    GtkWidget *dev_name;
    GtkWidget *dev_address;
    GtkWidget *dev_state;
    GtkWidget *dev_connect;
    GtkWidget *dev_battery_row;
    GtkWidget *dev_battery_bar;
    GtkWidget *dev_battery_label;
    GtkWidget *dev_status_row;
    GtkWidget *dev_sync_btn;
    GtkWidget *dev_find_btn;
    GtkWidget *dev_weather_row;
    GtkWidget *dev_weather_btn;
    GtkWidget *dev_find_phone_row;
    GtkWidget *dev_notif_switch;
    GtkWidget *dev_notif_test;
    GtkWidget *dev_profile_rows[5];
};

/* Pages */
GtkWidget *mi_today_page_new(AppState *state, MiwearDashboardUi *dash);
void mi_today_refresh(AppState *state);
void mi_today_redraw(AppState *state);
GtkWidget *mi_sleep_page_new(AppState *state, MiwearDashboardUi *dash);
void mi_sleep_refresh(AppState *state);
GtkWidget *mi_workouts_page_new(AppState *state, MiwearDashboardUi *dash);
void mi_workouts_refresh_training(AppState *state);
GtkWidget *mi_device_page_new(AppState *state, MiwearDashboardUi *dash);
void mi_device_refresh(AppState *state);

/* Date navigation shared by Today and Sleep. */
GtkWidget *mi_date_nav_new(AppState *state, MiwearDashboardUi *dash);
GDate *mi_view_date(AppState *state);
gchar *mi_day_key(GDate *d);
gboolean mi_viewing_today(AppState *state);
void mi_set_view_date(AppState *state, GDate *d);

/* Shell helpers */
void mi_toast(AppState *state, const char *text);
gboolean mi_is_connected(AppState *state);
void mi_connect_saved(AppState *state);
void mi_show_log_dialog(AppState *state);
void mi_show_weight_dialog(AppState *state);
void mi_show_page(AppState *state, const char *name);

/* Implemented in ui.c */
void miwear_ui_show_setup(AppState *state);
gboolean miwear_ui_has_saved_device(AppState *state);

#endif
