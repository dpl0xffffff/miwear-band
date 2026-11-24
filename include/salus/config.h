#ifndef SALUS_CONFIG_H
#define SALUS_CONFIG_H

#include "salus/app.h"

/* Load ~/.config/salus/config.ini (GKeyFile). Returns FALSE if missing. */
gboolean salus_config_load(SalusApp *state, const char *path);

/* Persist address + auth key (creates parent dirs). */
gboolean salus_config_save(const SalusApp *state, const char *path);

const char *salus_config_default_path(void);

#endif
