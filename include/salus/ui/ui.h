#ifndef SALUS_UI_H
#define SALUS_UI_H

#include "salus/app.h"

void salus_activate(GApplication *application, gpointer user_data);
void salus_app_shutdown(GApplication *app, gpointer user_data);

#endif
