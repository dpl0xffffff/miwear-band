#ifndef SALUS_MIGRATE_H
#define SALUS_MIGRATE_H

#include <glib.h>

/* Where Salus keeps its files (caller frees). */
gchar *salus_config_file_path(void);  /* ~/.config/salus/config.ini */
gchar *salus_database_path(void);     /* ~/.local/share/salus/salus.db */

/* First run after the rename from miwear-band: copies the old settings and database to the new
 * locations when those don't exist yet. The old files are left untouched. */
void salus_migrate_legacy_files(void);

#endif
