#include "miwear/migrate.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <sqlite3.h>

/* Before the app became Salus it kept its files under these names. */
#define LEGACY_CONFIG_DIR "miwear-band"
#define LEGACY_DATA_DIR "mi-band10-viewer"
#define LEGACY_DB_FILE "gadgetbridge_compat.db"

static gboolean copy_file(const char *from, const char *to) {
    gchar *data = NULL;
    gsize len = 0;
    if (!g_file_get_contents(from, &data, &len, NULL)) {
        return FALSE;
    }
    const gboolean ok = g_file_set_contents(to, data, (gssize)len, NULL);
    g_free(data);
    return ok;
}

/* SQLite's backup API copies a consistent snapshot, including pages still in the -wal file. */
static gboolean copy_database(const char *from, const char *to) {
    sqlite3 *src = NULL, *dst = NULL;
    gboolean ok = FALSE;
    if (sqlite3_open_v2(from, &src, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK &&
        sqlite3_open_v2(to, &dst, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) == SQLITE_OK) {
        sqlite3_backup *b = sqlite3_backup_init(dst, "main", src, "main");
        if (b != NULL) {
            ok = sqlite3_backup_step(b, -1) == SQLITE_DONE;
            ok = sqlite3_backup_finish(b) == SQLITE_OK && ok;
        }
    }
    sqlite3_close(src);
    sqlite3_close(dst);
    if (!ok) {
        g_unlink(to);
    }
    return ok;
}

void salus_migrate_legacy_files(void) {
    gchar *config = salus_config_file_path();
    if (!g_file_test(config, G_FILE_TEST_EXISTS)) {
        gchar *old = g_build_filename(g_get_user_config_dir(), LEGACY_CONFIG_DIR, "config.ini", NULL);
        if (g_file_test(old, G_FILE_TEST_IS_REGULAR)) {
            gchar *dir = g_path_get_dirname(config);
            g_mkdir_with_parents(dir, 0700);
            g_free(dir);
            if (copy_file(old, config)) {
                g_message("Copied settings from %s", old);
            }
        }
        g_free(old);
    }
    g_free(config);

    gchar *db = salus_database_path();
    if (!g_file_test(db, G_FILE_TEST_EXISTS)) {
        gchar *old = g_build_filename(g_get_user_data_dir(), LEGACY_DATA_DIR, LEGACY_DB_FILE, NULL);
        if (g_file_test(old, G_FILE_TEST_IS_REGULAR)) {
            gchar *dir = g_path_get_dirname(db);
            g_mkdir_with_parents(dir, 0755);
            g_free(dir);
            if (copy_database(old, db)) {
                g_message("Copied health data from %s", old);
            } else {
                g_warning("Could not copy health data from %s", old);
            }
        }
        g_free(old);
    }
    g_free(db);
}

gchar *salus_config_file_path(void) {
    return g_build_filename(g_get_user_config_dir(), "salus", "config.ini", NULL);
}

gchar *salus_database_path(void) {
    return g_build_filename(g_get_user_data_dir(), "salus", "salus.db", NULL);
}
