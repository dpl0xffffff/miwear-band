#ifndef SALUS_ACTIVITY_SCHEMA_READER_H
#define SALUS_ACTIVITY_SCHEMA_READER_H

#include <json-glib/json-glib.h>
#include <stddef.h>
#include <stdint.h>
#include <glib.h>

#include "salus/activity/workout_types.h"

/* Root directory with extracted schemas.zip (see CMake SALUS_SCHEMA_DIR). */
const char *salus_schema_dir(void);

/* Relative path from index.json, or NULL. Caller g_free()s result. */
gchar *salus_schema_resolve_sport(int sport_type, int file_type, int version);

gchar *salus_schema_resolve_daily(int daily_type, int file_type, int version);

gboolean salus_schema_load_root(const char *schema_json_path, JsonNode **out_root);

/* Mi Fitness constructSchemaBinaryData for sport (7-byte id + 0 + tail). */
GBytes *salus_schema_sport_wire_buffer(const uint8_t *payload, size_t payload_len);

/*
 * Decode sequential binary layout described by schema JSON into a JsonNode tree.
 * Returns FALSE on truncation or unsupported construct.
 */
gboolean salus_schema_decode(const JsonNode *schema_root,
                              const uint8_t *buffer,
                              size_t buffer_len,
                              JsonNode **out_decoded);


/* Save raw payload when SALUS_CAPTURE_DIR is set (for schema_binary_dump.py). */
void salus_schema_maybe_capture(const uint8_t *payload,
                                 size_t payload_len,
                                 int sport_type,
                                 int detail,
                                 int version,
                                 uint32_t file_ts);

#endif
