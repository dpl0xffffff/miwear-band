#ifndef SALUS_ACTIVITY_MI_FITNESS_REPORT_TABLES_H
#define SALUS_ACTIVITY_MI_FITNESS_REPORT_TABLES_H

/* Tables extracted from Mi Fitness (tools/mi-fitness-extract); implementation is generated. */

#include "salus/devices/xiaomi/mi_fitness_oneden.h"

#include <stddef.h>
#include <stdint.h>

#define MI_FITNESS_FIELD_UNKNOWN (-1000)

/* One sub-field of a FourDimen record item (FitnessDataBaseParser.DataItemLen). byte_size equal to
 * the item's size means "the whole value", else bits [start_bit, start_bit + bit_size). */
typedef struct {
    uint8_t present;
    uint8_t byte_size;
    uint8_t start_bit;
    uint8_t bit_size;
} MiFitnessItemLen;

#define MI_FITNESS_NO_TYPE INT16_MIN

/* FitnessDataBaseParser.FourDimenDataInfo: one 1/2/4-byte item holding up to three record values
 * (high / middle / low), each with its own validity bit. */
typedef struct {
    int16_t high, middle, low; /* record item ids (SportRecordBaseParser.DataItemType); MI_FITNESS_NO_TYPE = none */
    uint8_t byte_size;
    MiFitnessItemLen h, m, l;
    uint8_t support_version;
    int16_t max_support_version; /* -1 = no upper bound */
    uint8_t is_unsigned;
} MiFitnessFourDimenField;

typedef struct {
    uint8_t four_dimen; /* 1: `four` is used, 0: `one` */
    const MiFitnessFourDimenField *four;
    const MiFitnessOneDimenField *one;
    size_t count;
} MiFitnessRecordLayout;

/* A Mi Fitness *RecordParser: per-version record layout plus the per-segment header tables. */
typedef struct {
    const char *parser;
    uint8_t group;    /* GroupSportRecordParser */
    uint8_t swimming; /* records carry their own end time (lap records) */
    int distance_times;
    const MiFitnessOneDimenField *init; /* getInitDataTypeArray (start altitude, ski path) */
    size_t init_count;
    const MiFitnessOneDimenField *it_summary; /* getITSummaryDataType (pause state, totals, diving) */
    size_t it_summary_count;
    const MiFitnessRecordLayout *layouts; /* index = version - 1 */
    int max_version;
} MiFitnessRecordParser;

/* Record parser for a sport type (FitnessDataParser.getSportRecordParserInstance), NULL if none. */
const MiFitnessRecordParser *mi_fitness_record_parser(int sport_type);
/* Layout used for `version`, NULL if Mi Fitness has none (parseRecord returns no table). */
const MiFitnessRecordLayout *mi_fitness_record_layout(const MiFitnessRecordParser *p, int version);

/* Native report field table for a sport type (FitnessDataParser.getSportReportParserInstance). */
const MiFitnessOneDimenField *mi_fitness_report_fields(int sport_type, size_t *count, const char **parser);
/* Report field id (SportReportBaseParser.ReportType) for a Mi Fitness schema field name. */
int mi_fitness_schema_field_id(const char *name);
/* English name of a FitnessSportType (1–26 and the 100+ specific sports), NULL if unknown. */
const char *mi_fitness_sport_type_name(int sport_type);

#endif
