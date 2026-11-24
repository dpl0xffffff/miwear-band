#include "salus/activity/workout_record.h"

#include "salus/activity/mi_fitness_sport_record.h"
#include "salus/activity/schema_reader.h"

#include <json-glib/json-glib.h>
#include <math.h>

static gboolean decode_sport_schema(const char *schema_rel,
                                    const uint8_t *payload,
                                    size_t payload_len,
                                    JsonNode **out_decoded) {
    if (schema_rel == NULL || out_decoded == NULL) {
        return FALSE;
    }
    gchar *schema_path = g_build_filename(salus_schema_dir(), schema_rel, NULL);
    GBytes *wire = salus_schema_sport_wire_buffer(payload, payload_len);
    if (wire == NULL) {
        g_free(schema_path);
        return FALSE;
    }
    JsonNode *schema = NULL;
    if (!salus_schema_load_root(schema_path, &schema)) {
        g_bytes_unref(wire);
        g_free(schema_path);
        return FALSE;
    }
    gsize sz;
    const guint8 *buf = g_bytes_get_data(wire, &sz);
    JsonNode *decoded = NULL;
    const gboolean ok = salus_schema_decode(schema, buf, sz, &decoded);
    json_node_unref(schema);
    g_bytes_unref(wire);
    g_free(schema_path);
    if (!ok || decoded == NULL) {
        return FALSE;
    }
    *out_decoded = decoded;
    return TRUE;
}

gboolean salus_parse_workout_record(const uint8_t *payload,
                                     size_t payload_len,
                                     int sport_subtype,
                                     int version,
                                     SalusWorkoutSummary *summary) {
    if (payload == NULL || summary == NULL || payload_len < 16) {
        return FALSE;
    }
    /* Every sport/version Mi Fitness has a record schema for also has a native layout, and the
     * two describe the same bytes, so the native tables alone cover what the app can decode. */
    GArray *records = g_array_new(FALSE, FALSE, sizeof(MiFitnessRecord));
    GArray *segments = g_array_new(FALSE, FALSE, sizeof(MiFitnessRecordSegment));
    const gboolean ok =
        mi_fitness_sport_record_decode(payload, payload_len, sport_subtype, version, records, segments, NULL);
    if (ok) {
        mi_fitness_sport_record_apply(sport_subtype, records, segments, summary);
    }
    g_array_unref(records);
    g_array_unref(segments);
    return ok && summary->record_samples != NULL && summary->record_samples->len > 0;
}

/* A number either directly at @obj.@name, or inside a bit field of the same name
 * ({"latitude": {"latitude": 47.3}}, as schema bit fields decode). */
static gboolean gps_number(JsonObject *obj, const char *name, double *out) {
    if (obj == NULL || !json_object_has_member(obj, name)) {
        return FALSE;
    }
    JsonNode *n = json_object_get_member(obj, name);
    if (JSON_NODE_HOLDS_OBJECT(n)) {
        return gps_number(json_node_get_object(n), name, out);
    }
    if (!JSON_NODE_HOLDS_VALUE(n)) {
        return FALSE;
    }
    const GType t = json_node_get_value_type(n);
    if (t == G_TYPE_DOUBLE) {
        *out = json_node_get_double(n);
    } else if (t == G_TYPE_INT64) {
        *out = (double)json_node_get_int(n);
    } else {
        return FALSE;
    }
    return TRUE;
}

static gboolean extract_gps_points(JsonNode *decoded, SalusWorkoutSummary *summary) {
    if (decoded == NULL || summary == NULL || json_node_get_node_type(decoded) != JSON_NODE_OBJECT) {
        return FALSE;
    }
    JsonObject *root = json_node_get_object(decoded);
    if (!json_object_has_member(root, "body")) {
        return FALSE;
    }
    JsonObject *body = json_object_get_object_member(root, "body");
    if (body == NULL) {
        return FALSE;
    }
    JsonArray *pts = NULL;
    if (json_object_has_member(body, "dataPoints")) {
        pts = json_object_get_array_member(body, "dataPoints");
    } else if (json_object_has_member(body, "gpsPoints")) {
        pts = json_object_get_array_member(body, "gpsPoints");
    }
    if (pts == NULL || json_array_get_length(pts) == 0) {
        return FALSE;
    }
    if (summary->gps_track != NULL) {
        g_array_unref(summary->gps_track);
    }
    summary->gps_track = g_array_new(FALSE, FALSE, sizeof(SalusWorkoutGpsPoint));
    for (guint i = 0; i < json_array_get_length(pts); i++) {
        JsonObject *pt = json_array_get_object_element(pts, i);
        if (pt == NULL) {
            continue;
        }
        JsonObject *src = pt;
        if (json_object_has_member(pt, "latLng") && JSON_NODE_HOLDS_OBJECT(json_object_get_member(pt, "latLng"))) {
            src = json_object_get_object_member(pt, "latLng");
        } else if (json_object_has_member(pt, "location") &&
                   JSON_NODE_HOLDS_OBJECT(json_object_get_member(pt, "location"))) {
            src = json_object_get_object_member(pt, "location");
        }
        SalusWorkoutGpsPoint gp = {0};
        if (!gps_number(src, "latitude", &gp.lat) || !gps_number(src, "longitude", &gp.lon)) {
            continue;
        }
        /* Points without a fix come through as 0/0; anything outside the globe is not a fix either. */
        if ((gp.lat == 0.0 && gp.lon == 0.0) || fabs(gp.lat) > 90.0 || fabs(gp.lon) > 180.0) {
            continue;
        }
        g_array_append_val(summary->gps_track, gp);
    }
    return summary->gps_track != NULL && summary->gps_track->len > 0;
}

gboolean salus_parse_workout_gps(const uint8_t *payload,
                                  size_t payload_len,
                                  int sport_subtype,
                                  int version,
                                  SalusWorkoutSummary *summary) {
    if (payload == NULL || summary == NULL || payload_len < 8) {
        return FALSE;
    }
    int versions[5];
    size_t nv = 0;
    versions[nv++] = version;
    for (int v = 5; v >= 3 && nv < G_N_ELEMENTS(versions); v--) {
        if (v == version) {
            continue;
        }
        versions[nv++] = v;
    }
    for (size_t i = 0; i < nv; i++) {
        gchar *rel = salus_schema_resolve_sport(sport_subtype, 2, versions[i]);
        if (rel == NULL) {
            continue;
        }
        JsonNode *decoded = NULL;
        if (decode_sport_schema(rel, payload, payload_len, &decoded)) {
            if (extract_gps_points(decoded, summary)) {
                json_node_unref(decoded);
                g_free(rel);
                return TRUE;
            }
            json_node_unref(decoded);
        }
        g_free(rel);
    }
    return FALSE;
}
