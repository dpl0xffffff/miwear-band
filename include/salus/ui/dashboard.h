#ifndef SALUS_UI_DASHBOARD_H
#define SALUS_UI_DASHBOARD_H

#include "salus/app.h"
#include "salus/store/workout.h"

typedef struct SalusDashboardUi SalusDashboardUi;

SalusDashboardUi *salus_dashboard_ui_new(void);
void salus_dashboard_ui_free(SalusDashboardUi *dash);

GtkWidget *salus_dashboard_build(SalusApp *state, SalusDashboardUi *dash);
void salus_dashboard_refresh(SalusApp *state);
void salus_dashboard_schedule_refresh(SalusApp *state);
void salus_dashboard_set_notification_status(SalusApp *state, const char *msg);
void salus_dashboard_redraw_charts(SalusApp *state);
void salus_dashboard_update_device_state(SalusApp *state,
                                          const gchar *charging,
                                          const gchar *wearing,
                                          const gchar *sleep);

/* Adds or updates a workout. @name labels it when @summary is NULL or has no sport_name. */
void salus_sport_add_entry(SalusApp *state,
                           uint32_t ts,
                           int type,
                           int subtype,
                           const SalusWorkoutSummary *summary,
                           const char *name);
void salus_sport_load_from_db(SalusApp *state);
/* Frees all sport entries (and closes a workout detail view that points into them). */
void salus_sport_entries_clear(SalusApp *state);
/* Attaches a record/GPS track (@patch->record_samples / gps_track) to a workout, creating it
 * (named @patch->sport_name) when its summary hasn't arrived yet. */
void salus_sport_merge_attachment(SalusApp *state,
                                   uint32_t ts,
                                   int subtype,
                                   SalusWorkoutSummary *patch);
void salus_sport_rebuild_list(SalusApp *state);

/* Shell integration (ui.c) */
void salus_dashboard_add_breakpoints(GtkWidget *window, SalusDashboardUi *dash);
void salus_dashboard_set_toast_overlay(SalusDashboardUi *dash, GtkWidget *overlay);
void salus_dashboard_toast(SalusApp *state, const char *text);
void salus_dashboard_set_busy(SalusApp *state, gboolean busy);
void salus_dashboard_set_status(SalusApp *state, const char *text);
void salus_dashboard_update_connection(SalusApp *state);

#endif
