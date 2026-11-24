#ifndef MIWEAR_UI_DASHBOARD_H
#define MIWEAR_UI_DASHBOARD_H

#include "miwear/app.h"
#include "miwear/activity/workout_summary.h"

typedef struct MiwearDashboardUi MiwearDashboardUi;

MiwearDashboardUi *miwear_dashboard_ui_new(void);
void miwear_dashboard_ui_free(MiwearDashboardUi *dash);

GtkWidget *miwear_dashboard_build(AppState *state, MiwearDashboardUi *dash);
void miwear_dashboard_refresh(AppState *state);
void miwear_dashboard_schedule_refresh(AppState *state);
void miwear_dashboard_set_notification_status(AppState *state, const char *msg);
void miwear_dashboard_redraw_charts(AppState *state);
void miwear_dashboard_update_device_state(AppState *state,
                                          const gchar *charging,
                                          const gchar *wearing,
                                          const gchar *sleep);

void miwear_sport_add_entry(AppState *state,
                            uint32_t ts,
                            int type,
                            int subtype,
                            const MiwearWorkoutSummary *summary);
void miwear_sport_load_from_db(AppState *state);
/* Frees all sport entries (and closes a workout detail view that points into them). */
void miwear_sport_entries_clear(AppState *state);
void miwear_sport_merge_attachment(AppState *state,
                                   uint32_t ts,
                                   int subtype,
                                   MiwearWorkoutSummary *patch);
void miwear_sport_rebuild_list(AppState *state);

/* Shell integration (ui.c) */
void miwear_dashboard_add_breakpoints(GtkWidget *window, MiwearDashboardUi *dash);
void miwear_dashboard_set_toast_overlay(MiwearDashboardUi *dash, GtkWidget *overlay);
void miwear_dashboard_toast(AppState *state, const char *text);
void miwear_dashboard_set_busy(AppState *state, gboolean busy);
void miwear_dashboard_set_status(AppState *state, const char *text);
void miwear_dashboard_update_connection(AppState *state);

#endif
