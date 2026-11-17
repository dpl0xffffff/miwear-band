#ifndef MIWEAR_ACTIVITY_MI_FITNESS_REPORT_TABLES_H
#define MIWEAR_ACTIVITY_MI_FITNESS_REPORT_TABLES_H

/* Tables extracted from Mi Fitness (tools/mi-fitness-extract); implementation is generated. */

#include "miwear/activity/mi_fitness_oneden.h"

#include <stddef.h>

#define MI_FITNESS_FIELD_UNKNOWN (-1000)

/* Native report field table for a sport type (FitnessDataParser.getSportReportParserInstance). */
const MiFitnessOneDimenField *mi_fitness_report_fields(int sport_type, size_t *count, const char **parser);
/* Report field id (SportReportBaseParser.ReportType) for a Mi Fitness schema field name. */
int mi_fitness_schema_field_id(const char *name);
/* English name of a FitnessSportType (1–26 and the 100+ specific sports), NULL if unknown. */
const char *mi_fitness_sport_type_name(int sport_type);

#endif
