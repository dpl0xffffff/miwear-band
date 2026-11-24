#include "salus/activity/schema_reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *argv0) {
    fprintf(stderr,
            "Usage:\n"
            "  %s --bin FILE (--schema REL.json | --sport N --detail D --version V)\n"
            "  %s --bin FILE --sport 22 --detail 1 --version 8 --summary\n\n"
            "Environment: SALUS_SCHEMA_DIR (extracted schemas.zip)\n",
            argv0,
            argv0);
}

int main(int argc, char **argv) {
    const char *bin_path = NULL;
    const char *schema_rel = NULL;
    int sport = -1;
    int detail = -1;
    int version = -1;
    gboolean summary = FALSE;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--bin") == 0 && i + 1 < argc) {
            bin_path = argv[++i];
        } else if (strcmp(argv[i], "--schema") == 0 && i + 1 < argc) {
            schema_rel = argv[++i];
        } else if (strcmp(argv[i], "--sport") == 0 && i + 1 < argc) {
            sport = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--detail") == 0 && i + 1 < argc) {
            detail = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--version") == 0 && i + 1 < argc) {
            version = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--summary") == 0) {
            summary = TRUE;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        }
    }

    if (bin_path == NULL) {
        usage(argv[0]);
        return 1;
    }

    gsize len = 0;
    GError *err = NULL;
    gchar *payload = NULL;
    if (!g_file_get_contents(bin_path, &payload, &len, &err)) {
        fprintf(stderr, "read %s: %s\n", bin_path, err->message);
        g_clear_error(&err);
        return 1;
    }

    gchar *rel = schema_rel != NULL ? g_strdup(schema_rel) : NULL;
    if (rel == NULL) {
        if (sport < 0 || detail < 0 || version < 0) {
            fprintf(stderr, "Need --schema or --sport/--detail/--version\n");
            g_free(payload);
            return 1;
        }
        rel = salus_schema_resolve_sport(sport, detail, version);
        if (rel == NULL) {
            fprintf(stderr, "No schema index entry (legacy nativeParse only).\n");
            g_free(payload);
            return 2;
        }
    }

    gchar *schema_path = g_build_filename(salus_schema_dir(), rel, NULL);
    g_free(rel);

    const uint8_t *payload_bytes = (const uint8_t *)payload;

    GBytes *wire = salus_schema_sport_wire_buffer(payload_bytes, len);
    if (wire == NULL) {
        fprintf(stderr, "wire buffer too short\n");
        g_free(schema_path);
        return 1;
    }

    JsonNode *schema = NULL;
    if (!salus_schema_load_root(schema_path, &schema)) {
        fprintf(stderr, "Failed to load schema %s (extract schemas.zip to SALUS_SCHEMA_DIR)\n", schema_path);
        g_bytes_unref(wire);
        g_free(schema_path);
        return 1;
    }

    gsize wlen;
    const guint8 *wdata = g_bytes_get_data(wire, &wlen);
    JsonNode *decoded = NULL;
    if (!salus_schema_decode(schema, wdata, wlen, &decoded)) {
        fprintf(stderr, "Decode failed (truncated or unsupported field)\n");
        json_node_unref(schema);
        g_bytes_unref(wire);
        g_free(schema_path);
        return 1;
    }
    json_node_unref(schema);
    g_bytes_unref(wire);

    JsonGenerator *gen = json_generator_new();
    json_generator_set_root(gen, decoded);
    json_generator_set_pretty(gen, TRUE);
    gchar *text = json_generator_to_data(gen, NULL);
    printf("%s\n", text);
    g_free(text);

    if (summary && json_node_get_node_type(decoded) == JSON_NODE_OBJECT) {
        JsonObject *root = json_node_get_object(decoded);
        if (json_object_has_member(root, "body")) {
            JsonObject *body = json_object_get_object_member(root, "body");
            printf("\n--- workout summary (body fields) ---\n");
            printf("duration=%lld s distance=%lld m kcal=%lld HR %lld/%lld/%lld\n",
                   json_object_has_member(body, "totalDuration")
                       ? (long long)json_object_get_int_member(body, "totalDuration")
                       : 0LL,
                   json_object_has_member(body, "totalDistance")
                       ? (long long)json_object_get_int_member(body, "totalDistance")
                       : 0LL,
                   json_object_has_member(body, "totalCalories")
                       ? (long long)json_object_get_int_member(body, "totalCalories")
                       : 0LL,
                   json_object_has_member(body, "avgHeartRate")
                       ? (long long)json_object_get_int_member(body, "avgHeartRate")
                       : 0LL,
                   json_object_has_member(body, "maxHeartRate")
                       ? (long long)json_object_get_int_member(body, "maxHeartRate")
                       : 0LL,
                   json_object_has_member(body, "minHeartRate")
                       ? (long long)json_object_get_int_member(body, "minHeartRate")
                       : 0LL);
        }
    }

    g_free(schema_path);
    g_free(payload);
    json_node_unref(decoded);
    g_object_unref(gen);
    return 0;
}
