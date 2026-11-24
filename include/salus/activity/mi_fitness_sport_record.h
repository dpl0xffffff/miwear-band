#ifndef SALUS_ACTIVITY_MI_FITNESS_SPORT_RECORD_H
#define SALUS_ACTIVITY_MI_FITNESS_SPORT_RECORD_H

/*
 * Workout time series ("record", file type 0) decoding, as Mi Fitness does it
 * (SportRecordBaseParser / GroupSportRecordParser / FourDimenDataParser):
 *
 *   file    = 7-byte file id + 1 byte + validity bitmap (FitnessDataValidity) + body + CRC32
 *   body    = segments (one per stretch between pauses), each:
 *               init data (OneDimen, e.g. start altitude) | u32 record count | u32 start time |
 *               interval-training summary (OneDimen) | records
 *             group sports (rope skipping, rowing, HIIT) without a summary use
 *               u32 count | u32 start | u8 is_group | u32 group record count | records [| rest records]
 *   record  = the version's layout: OneDimen fields, or FourDimen items (1/2/4 bytes split into up to
 *             three bit fields, each with its own validity nibble bit)
 *
 * Each record spans one second unless it carries its own end time (item 1: swim laps).
 * Values are keyed by SportRecordBaseParser.DataItemType ids.
 */

#include "salus/activity/mi_fitness_oneden.h"
#include "salus/activity/workout_types.h"

#include <glib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MI_FITNESS_RECORD_VALUE_MAX 32

typedef struct {
    int type;
    int64_t value;
} MiFitnessRecordValue;

typedef struct {
    uint32_t start_sec; /* band time, like the report's start time */
    uint32_t end_sec;
    uint16_t segment;   /* index of the segment (pause-separated stretch) */
    uint8_t count;
    MiFitnessRecordValue values[MI_FITNESS_RECORD_VALUE_MAX];
} MiFitnessRecord;

typedef struct {
    uint32_t start_sec;
    uint32_t record_count;
    bool has_height; /* init data: altitude at the segment start */
    float height_m;
} MiFitnessRecordSegment;

/* Decodes a record file into `records` (MiFitnessRecord) and optionally `segments`
 * (MiFitnessRecordSegment). FALSE when the sport/version has no Mi Fitness layout or the file is
 * malformed. `parser_out` (optional) names the Mi Fitness parser used. */
bool mi_fitness_sport_record_decode(const uint8_t *payload,
                                    size_t payload_len,
                                    int sport_type,
                                    int version,
                                    GArray *records,
                                    GArray *segments,
                                    const char **parser_out);

/* Value of item `type` in a record, FALSE if absent / invalid. */
bool mi_fitness_record_get(const MiFitnessRecord *r, int type, int64_t *value);

/* Turns decoded records into out->record_samples (offsets from the first segment start), applying
 * SportRecordBaseParser.setSportRecordItemValue's units. */
void mi_fitness_sport_record_apply(int sport_type,
                                   const GArray *records,
                                   const GArray *segments,
                                   SalusWorkoutSummary *out);

#endif
