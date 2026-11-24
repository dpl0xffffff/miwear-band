#ifndef MIWEAR_CONFIG_H
#define MIWEAR_CONFIG_H

#include "miwear/app.h"

/* Load ~/.config/salus/config.ini (GKeyFile). Returns FALSE if missing. */
gboolean miwear_config_load(AppState *state, const char *path);

/* Persist address + auth key (creates parent dirs). */
gboolean miwear_config_save(const AppState *state, const char *path);

const char *miwear_config_default_path(void);

#endif
