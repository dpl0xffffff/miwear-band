#ifndef MIWEAR_ACTIVITY_SCHEMA_READER_H
#define MIWEAR_ACTIVITY_SCHEMA_READER_H

#include <json-glib/json-glib.h>
#include <stddef.h>
#include <stdint.h>
#include <glib.h>

#include "miwear/activity/workout_types.h"

/* Root directory with extracted schemas.zip (see CMake MIWEAR_SCHEMA_DIR). */
const char *miwear_schema_dir(void);

/* Relative path from index.json, or NULL. Caller g_free()s result. */
gchar *miwear_schema_resolve_sport(int sport_type, int file_type, int version);

gchar *miwear_schema_resolve_daily(int daily_type, int file_type, int version);

gboolean miwear_schema_load_root(const char *schema_json_path, JsonNode **out_root);

/* Mi Fitness constructSchemaBinaryData for sport (7-byte id + 0 + tail). */
GBytes *miwear_schema_sport_wire_buffer(const uint8_t *payload, size_t payload_len);

/*
 * Decode sequential binary layout described by schema JSON into a JsonNode tree.
 * Returns FALSE on truncation or unsupported construct.
 */
gboolean miwear_schema_decode(const JsonNode *schema_root,
                              const uint8_t *buffer,
                              size_t buffer_len,
                              JsonNode **out_decoded);

gboolean miwear_schema_parse_sport_report(const char *schema_json_path,
                                          const uint8_t *payload,
                                          size_t payload_len,
                                          MiwearWorkoutSummary *out);

/* Save raw payload when MIWEAR_CAPTURE_DIR is set (for schema_binary_dump.py). */
void miwear_schema_maybe_capture(const uint8_t *payload,
                                 size_t payload_len,
                                 int sport_type,
                                 int detail,
                                 int version,
                                 uint32_t file_ts);

#endif
