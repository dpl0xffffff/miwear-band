/* Verifies SalusLocalDayCursor against the reference GDateTime conversion it replaces.
 * Run under several TZ values (see CMakeLists.txt), including zones with DST and 30-minute shifts. */
#include "salus/local_time.h"

#include <stdio.h>
#include <string.h>

static gboolean reference(gint64 t, gchar **day_key, guint16 *mod) {
    GDateTime *dt = g_date_time_new_from_unix_local(t);
    if (dt == NULL) {
        return FALSE;
    }
    *day_key = g_date_time_format(dt, "%Y-%m-%d");
    *mod = (guint16)(g_date_time_get_hour(dt) * 60 + g_date_time_get_minute(dt));
    g_date_time_unref(dt);
    return TRUE;
}

static guint64 checked;
static guint64 failures;

static void check(SalusLocalDayCursor *c, gint64 t) {
    gchar *ref_key = NULL;
    guint16 ref_mod = 0;
    const gboolean ref_ok = reference(t, &ref_key, &ref_mod);
    const gchar *key = NULL;
    guint16 mod = 0;
    const gboolean ok = salus_local_day_cursor_resolve(c, t, &key, &mod);
    checked++;
    if (ok != ref_ok || (ok && (strcmp(key, ref_key) != 0 || mod != ref_mod))) {
        if (failures++ < 10) {
            fprintf(stderr, "MISMATCH t=%" G_GINT64_FORMAT ": got %s %u, want %s %u\n",
                    t, ok ? key : "(fail)", mod, ref_ok ? ref_key : "(fail)", ref_mod);
        }
    }
    g_free(ref_key);
}

int main(void) {
    SalusLocalDayCursor c;
    salus_local_day_cursor_init(&c);
    GTimeZone *tz = g_time_zone_new_local();

    const gint64 from = 1546300800; /* 2019-01-01 UTC */
    const gint64 to = 1830297600;   /* 2028-01-01 UTC */

    /* Sorted sweep with an odd stride (hits every minute/second phase over time). */
    for (gint64 t = from; t < to; t += 433) {
        check(&c, t);
    }

    /* Dense per-second scan ±2h around every offset transition. */
    gint prev = g_time_zone_find_interval(tz, G_TIME_TYPE_UNIVERSAL, from);
    for (gint64 day = from; day < to; day += 3600) {
        const gint cur = g_time_zone_find_interval(tz, G_TIME_TYPE_UNIVERSAL, day);
        if (cur != prev) {
            for (gint64 t = day - 3 * 3600; t < day + 2 * 3600; t++) {
                check(&c, t);
                /* Probe backwards inside the window just filled: catches a cached hour that
                 * straddles an offset change (e.g. Lord Howe's 30-minute DST shift). */
                check(&c, t - 1799);
                check(&c, t);
            }
            prev = cur;
        }
    }

    /* Unsorted access and pre-2020 garbage timestamps seen in real databases. */
    GRand *rng = g_rand_new_with_seed(42);
    for (int i = 0; i < 200000; i++) {
        check(&c, from + (gint64)g_rand_int_range(rng, 0, (gint32)(to - from)));
    }
    check(&c, -2040441916);
    check(&c, 0);
    check(&c, -1);
    g_rand_free(rng);

    g_time_zone_unref(tz);
    salus_local_day_cursor_clear(&c);
    printf("TZ=%s: %" G_GUINT64_FORMAT " checks, %" G_GUINT64_FORMAT " mismatches\n",
           g_getenv("TZ") != NULL ? g_getenv("TZ") : "(local)", checked, failures);
    return failures == 0 ? 0 : 1;
}
