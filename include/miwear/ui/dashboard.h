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
void miwear_dashboard_update_live(AppState *state,
                                  gboolean has_hr, guint hr,
                                  gboolean has_steps, guint steps,
                                  gboolean has_calories, guint calories,
                                  gboolean has_standing, guint standing,
                                  gboolean has_battery, guint battery,
                                  gboolean has_moving, guint moving,
                                  const gchar *spo2_text,
                                  const gchar *stress_text);
void miwear_dashboard_update_device_state(AppState *state,
                                          const gchar *charging,
                                          const gchar *wearing,
                                          const gchar *sleep);

void miwear_sport_add_entry(AppState *state,
                            uint32_t ts,
                            int type,
                            int subtype,
                            const MiwearWorkoutSummary *summary);
void miwear_dashboard_notify_summary(AppState *state, const MiwearDailySummaryRow *row);

void miwear_dashboard_apply_viewport(AppState *state, int width, int height);
double miwear_dashboard_ui_scale(MiwearDashboardUi *dash);

#endif
