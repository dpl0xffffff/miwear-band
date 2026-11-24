#include "salus/local_time.h"

void salus_local_day_cursor_init(SalusLocalDayCursor *c) {
    c->tz = g_time_zone_new_local();
    c->win_start = 0;
    c->win_end = 0;
    c->win_minute = 0;
    c->day_key[0] = '\0';
}

void salus_local_day_cursor_clear(SalusLocalDayCursor *c) {
    if (c->tz != NULL) {
        g_time_zone_unref(c->tz);
        c->tz = NULL;
    }
    c->win_end = c->win_start;
}

/* TRUE when the local UTC offset is the same at both ends of [start, end). */
static gboolean offset_constant(GTimeZone *tz, gint64 start, gint64 end) {
    const gint a = g_time_zone_find_interval(tz, G_TIME_TYPE_UNIVERSAL, start);
    const gint b = g_time_zone_find_interval(tz, G_TIME_TYPE_UNIVERSAL, end - 1);
    return a == b;
}

static gboolean cursor_refill(SalusLocalDayCursor *c, gint64 unix_sec) {
    GDateTime *dt = g_date_time_new_from_unix_local(unix_sec);
    if (dt == NULL) {
        return FALSE;
    }
    const gint hour = g_date_time_get_hour(dt);
    const gint minute = g_date_time_get_minute(dt);
    const gint second = g_date_time_get_second(dt);
    g_snprintf(c->day_key, sizeof(c->day_key), "%04d-%02d-%02d",
               g_date_time_get_year(dt), g_date_time_get_month(dt), g_date_time_get_day_of_month(dt));
    g_date_time_unref(dt);

    /* Prefer the whole local hour; shrink to the minute (then the second) around offset changes. */
    gint64 start = unix_sec - (gint64)(minute * 60 + second);
    gint64 len = 3600;
    guint16 start_minute = (guint16)(hour * 60);
    if (!offset_constant(c->tz, start, start + len)) {
        start = unix_sec - second;
        len = 60;
        start_minute = (guint16)(hour * 60 + minute);
        if (!offset_constant(c->tz, start, start + len)) {
            start = unix_sec;
            len = 1;
        }
    }
    c->win_start = start;
    c->win_end = start + len;
    c->win_minute = start_minute;
    return TRUE;
}

gboolean salus_local_day_cursor_resolve(SalusLocalDayCursor *c,
                                         gint64 unix_sec,
                                         const gchar **day_key_out,
                                         guint16 *minute_of_day_out) {
    if (unix_sec < c->win_start || unix_sec >= c->win_end) {
        if (!cursor_refill(c, unix_sec)) {
            return FALSE;
        }
    }
    if (day_key_out != NULL) {
        *day_key_out = c->day_key;
    }
    if (minute_of_day_out != NULL) {
        *minute_of_day_out = (guint16)(c->win_minute + (guint)((unix_sec - c->win_start) / 60));
    }
    return TRUE;
}

gchar *salus_local_day_key(gint64 unix_sec) {
    GDateTime *dt = g_date_time_new_from_unix_local(unix_sec);
    if (dt == NULL) {
        return NULL;
    }
    gchar *key = g_date_time_format(dt, "%Y-%m-%d");
    g_date_time_unref(dt);
    return key;
}
