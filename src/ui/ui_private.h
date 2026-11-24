#ifndef SALUS_UI_PRIVATE_H
#define SALUS_UI_PRIVATE_H

/* Shared between the UI translation units (dashboard, sleep, workouts, device, widgets). */

#include "salus/app.h"
#include "salus/ui/dashboard.h"
#include "salus/store/health_data.h"

#include <adwaita.h>
#include <cairo.h>

/* ---- Palette ------------------------------------------------------------------------------ */

typedef enum {
    UI_COLOR_HR,
    UI_COLOR_SPO2,
    UI_COLOR_STRESS,
    UI_COLOR_STEPS,
    UI_COLOR_CALORIES,
    UI_COLOR_ACTIVE,
    UI_COLOR_SLEEP,
    UI_COLOR_VITALITY,
    UI_COLOR_WEIGHT,
    UI_COLOR_CYCLES,
    UI_COLOR_DEEP,
    UI_COLOR_LIGHT,
    UI_COLOR_REM,
    UI_COLOR_AWAKE,
    UI_COLOR_COUNT
} UiColor;

/* CSS class carrying the colour as `color:` (for icons and accents). */
const char *ui_color_css_class(UiColor c);
void ui_color_rgb(UiColor c, double rgb[3]);
void ui_cairo_set_color(cairo_t *cr, UiColor c, double alpha);
/* Theme foreground of a widget, with alpha. */
void ui_cairo_set_fg(cairo_t *cr, GtkWidget *widget, double alpha);
gboolean ui_is_dark(void);

void ui_style_install(GdkDisplay *display);

/* ---- Small widget helpers ----------------------------------------------------------------- */

GtkWidget *ui_label(const char *text, const char *css_class, float xalign);
GtkWidget *ui_icon(const char *icon_name, UiColor color, int pixel_size);
/* Card container (.card) with padding; returns the inner vertical box via `inner_out`. */
GtkWidget *ui_card_new(GtkWidget **inner_out, int spacing);
/* "Title" + optional dimmed subtitle header row for a card. Returns the row box. */
GtkWidget *ui_card_header(const char *title, GtkWidget **subtitle_out, GtkWidget *end_widget);
/* Stat column: big value over a caption. */
GtkWidget *ui_stat_new(const char *caption, GtkWidget **value_out);
/* Wrapping row for stat columns: 2–4 per line depending on width. */
GtkWidget *ui_stat_flow_new(void);
void ui_stat_flow_append(GtkWidget *flow, GtkWidget *child);
void ui_format_thousands(long v, gchar *buf, gsize len);
void ui_format_minutes(int minutes, gchar *buf, gsize len); /* "7h 05m" / "42 min" */
void ui_format_clock(int minute_of_day, gchar *buf, gsize len); /* "07:05" */
void ui_clear_box(GtkWidget *box);
/* Vertically scrolling page whose content is centred and at most @max_width wide. */
GtkWidget *ui_page_scroll(GtkWidget *content, int max_width);

/* ---- Drawing helpers ---------------------------------------------------------------------- */

void ui_rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r);
void ui_draw_text(cairo_t *cr, GtkWidget *widget, const char *text, double x, double y,
                  double size, double alpha, double xalign);
/* Hover bubble right of @x (left of it when it would pass @right), its top at @top. */
void ui_draw_tooltip(cairo_t *cr, GtkWidget *widget, const char *text, double x, double top, double right);
/* Activity rings: values/goals for up to 3 rings, outermost first. */
void ui_draw_rings(cairo_t *cr, double cx, double cy, double radius, double thickness,
                   const double *ratios, const UiColor *colors, int n);

/* ---- Per-minute day series ---------------------------------------------------------------- */

#define UI_DAY_MINUTES 1440

typedef enum {
    UI_SERIES_HR,
    UI_SERIES_SPO2,
    UI_SERIES_STRESS,
    UI_SERIES_STEPS,
    UI_SERIES_COUNT
} UiSeries;

/* Copies the series for `day_key` into values[UI_DAY_MINUTES] (-1 = none). Returns the count. */
guint ui_series_collect(SalusApp *state, const gchar *day_key, UiSeries series, int *values);
/* Sparkline drawing area for a series of the viewed day. */
GtkWidget *ui_sparkline_new(SalusApp *state, UiSeries series);

/* ---- Sleep night window ------------------------------------------------------------------- */

#define UI_NIGHT_MINUTES 1440 /* 18:00 of the previous day → 18:00 of the wake day */
#define UI_NIGHT_START_MIN (18 * 60)

typedef struct {
    int stage[UI_NIGHT_MINUTES]; /* -1 = none, else normalised stage (2 deep … 5 awake, 1 unspec) */
    int first;                   /* first index with a stage, -1 when no sleep */
    int last;
    int deep, light, rem, awake, other;
    int total_asleep;            /* band total when synced, else counted */
    gboolean has_breakdown;
    gboolean has_timeline;       /* per-minute stages, not just "asleep" */
    /* Per-minute sums and sample counts, used for the night's averages. */
    int hr_sum[UI_NIGHT_MINUTES], hr_n[UI_NIGHT_MINUTES];
    int spo2_sum[UI_NIGHT_MINUTES], spo2_n[UI_NIGHT_MINUTES];
    int avg_hr;   /* over first … last; 0 when no samples */
    int avg_spo2; /* over first … last; 0 when no samples */
} UiNight;

void ui_night_collect(SalusApp *state, GDate *wake_day, UiNight *out);

/* ---- Dashboard state ---------------------------------------------------------------------- */

typedef struct {
    GtkWidget *root;
    GtkWidget *icon;
    GtkWidget *value;
    GtkWidget *unit;
    GtkWidget *sub;
    GtkWidget *spark;
} UiTile;

struct SalusDashboardUi {
    SalusApp *state;
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
    UiTile tile_hr, tile_spo2, tile_stress, tile_sleep, tile_vitality, tile_weight, tile_cycles;
    AdwNavigationPage *metric_page; /* chart of one UiSeries, pushed from a tile */
    GtkWidget *trend_area;
    GtkWidget *trend_subtitle;
    GtkWidget *trend_stat_value[4];
    GtkWidget *trend_stat_caption[4];
    int trend_metric;
    double trend_hover_x;

    /* Sleep */
    GtkWidget *sleep_total;
    GtkWidget *sleep_bar;
    GtkWidget *sleep_legend;
    GtkWidget *sleep_hypnogram;
    GtkWidget *sleep_week;
    GtkWidget *sleep_content;
    GtkWidget *sleep_empty;
    UiNight *night;
    int week_minutes[7];
    double sleep_hover_x;

    /* Workouts */
    GtkWidget *sport_list_box;
    GtkWidget *sport_empty;
    GtkWidget *sport_summary;
    SalusSportEntry *sport_detail_entry;
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
GtkWidget *ui_today_page_new(SalusApp *state, SalusDashboardUi *dash);
void ui_today_refresh(SalusApp *state);
void ui_today_redraw(SalusApp *state);
GtkWidget *mi_sleep_page_new(SalusApp *state, SalusDashboardUi *dash);
void mi_sleep_refresh(SalusApp *state);
GtkWidget *ui_workouts_page_new(SalusApp *state, SalusDashboardUi *dash);
void ui_workouts_refresh_training(SalusApp *state);
GtkWidget *ui_device_page_new(SalusApp *state, SalusDashboardUi *dash);
void ui_device_refresh(SalusApp *state);

/* Date navigation shared by Today and Sleep. */
GtkWidget *ui_date_nav_new(SalusApp *state, SalusDashboardUi *dash);
GDate *ui_view_date(SalusApp *state);
gchar *ui_day_key(GDate *d);
gboolean ui_viewing_today(SalusApp *state);
void ui_set_view_date(SalusApp *state, GDate *d);

/* Shell helpers */
void ui_toast(SalusApp *state, const char *text);
gboolean ui_is_connected(SalusApp *state);
void ui_connect_saved(SalusApp *state);
void ui_show_log_dialog(SalusApp *state);
void ui_show_weight_dialog(SalusApp *state);
void ui_show_page(SalusApp *state, const char *name);

/* Connect flow (connect.c): the "setup" page of the main stack. */
GtkWidget *salus_connect_build(SalusApp *app);
void salus_connect_free(SalusApp *app);
void salus_connect_show(SalusApp *app);
/* Leaves the flow (back to the dashboard when a device is saved). */
void salus_connect_cancel(SalusApp *app);
void salus_connect_set_busy(SalusApp *app, gboolean busy);
void salus_connect_set_status(SalusApp *app, const char *text);

/* Implemented in ui.c */
void salus_ui_show_setup(SalusApp *state);
gboolean salus_ui_has_saved_device(SalusApp *state);

#endif
