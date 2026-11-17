#ifndef MIWEAR_LOCAL_TIME_H
#define MIWEAR_LOCAL_TIME_H

#include <glib.h>

/*
 * Fast unix-seconds → local "YYYY-MM-DD" day key + minute-of-day conversion.
 *
 * Equivalent to g_date_time_new_from_unix_local() + g_date_time_format("%Y-%m-%d") +
 * hour * 60 + minute, but caches the current local hour so that sorted inputs (DB scans,
 * per-minute sleep timelines) do not allocate a GDateTime and format a string per sample.
 * The cache window is only used when the UTC offset is constant across it, so DST
 * transitions resolve exactly like GDateTime does.
 */
typedef struct {
    GTimeZone *tz;
    gint64 win_start;   /* unix seconds, inclusive */
    gint64 win_end;     /* unix seconds, exclusive; win_end <= win_start means empty */
    guint16 win_minute; /* local minute-of-day at win_start */
    gchar day_key[16];
} MiwearLocalDayCursor;

void miwear_local_day_cursor_init(MiwearLocalDayCursor *c);
void miwear_local_day_cursor_clear(MiwearLocalDayCursor *c);

/* Returns FALSE when unix_sec is not representable as a local date.
 * *day_key_out points into the cursor and stays valid until the next call. */
gboolean miwear_local_day_cursor_resolve(MiwearLocalDayCursor *c,
                                         gint64 unix_sec,
                                         const gchar **day_key_out,
                                         guint16 *minute_of_day_out);

/* One-shot helper (allocates); for call sites that convert a single timestamp. */
gchar *miwear_local_day_key(gint64 unix_sec);

#endif
