#ifndef MIWEAR_ACTIVITY_DAILY_DETAILS_H
#define MIWEAR_ACTIVITY_DAILY_DETAILS_H

#include <glib.h>
#include <stddef.h>
#include <stdint.h>

/* One minute of a daily-details activity file. -1 = not present in the record. */
typedef struct {
    int steps;
    int active_calories;
    int distance_cm;
    int heart_rate;
    int energy;
    int spo2;
    int stress;
} MiwearDailyDetailsMinute;

/* Header size (bytes after the 8-byte file id) for a daily-details file version, 0 if unknown. */
int miwear_daily_details_header_size(int version);

/*
 * Decodes a whole daily-details file (file id + header + records + CRC32), one entry per minute
 * starting at the file timestamp. Port of Gadgetbridge's DailyDetailsParser: the header size is
 * fixed by the file version. Returns NULL (and sets *error_out, if given) for an unknown version
 * or a payload too short to hold a header.
 */
GArray *miwear_daily_details_decode(int version, const uint8_t *payload, size_t payload_len, const char **error_out);

#endif
