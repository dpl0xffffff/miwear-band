#include "miwear/activity/workout_record.h"

#include "miwear/activity/schema_reader.h"

#include <json-glib/json-glib.h>

static gboolean decode_sport_schema(const char *schema_rel,
                                    const uint8_t *payload,
                                    size_t payload_len,
                                    JsonNode **out_decoded) {
    if (schema_rel == NULL || out_decoded == NULL) {
        return FALSE;
    }
    gchar *schema_path = g_build_filename(miwear_schema_dir(), schema_rel, NULL);
    GBytes *wire = miwear_schema_sport_wire_buffer(payload, payload_len);
    if (wire == NULL) {
        g_free(schema_path);
        return FALSE;
    }
    JsonNode *schema = NULL;
    if (!miwear_schema_load_root(schema_path, &schema)) {
        g_bytes_unref(wire);
        g_free(schema_path);
        return FALSE;
    }
    gsize sz;
    const guint8 *buf = g_bytes_get_data(wire, &sz);
    JsonNode *decoded = NULL;
    const gboolean ok = miwear_schema_decode(schema, buf, sz, &decoded);
    json_node_unref(schema);
    g_bytes_unref(wire);
    g_free(schema_path);
    if (!ok || decoded == NULL) {
        return FALSE;
    }
    *out_decoded = decoded;
    return TRUE;
}

static int json_int_member(JsonObject *o, const char *key) {
    if (o == NULL || !json_object_has_member(o, key)) {
        return 0;
    }
    return (int)json_object_get_int_member(o, key);
}

static gboolean extract_record_points(JsonNode *decoded, MiwearWorkoutSummary *summary) {
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
    } else if (json_object_has_member(body, "records")) {
        pts = json_object_get_array_member(body, "records");
    }
    if (pts == NULL || json_array_get_length(pts) == 0) {
        return FALSE;
    }

    miwear_workout_summary_free_series(summary);
    summary->record_samples = g_array_new(FALSE, FALSE, sizeof(MiwearWorkoutSample));
    const guint n = json_array_get_length(pts);
    int interval = 6;
    if (summary->duration_sec > 0 && n > 1) {
        interval = summary->duration_sec / (int)n;
        if (interval <= 0) {
            interval = 1;
        }
    }
    summary->record_interval_sec = interval;

    for (guint i = 0; i < n; i++) {
        JsonObject *pt = json_array_get_object_element(pts, i);
        if (pt == NULL) {
            continue;
        }
        MiwearWorkoutSample sample = {0};
        sample.offset_sec = (uint32_t)(i * (guint)interval);
        if (json_object_has_member(pt, "heartRate")) {
            JsonObject *hr = json_object_get_object_member(pt, "heartRate");
            if (hr != NULL) {
                sample.hr = (uint8_t)json_int_member(hr, "heartRate");
            }
        } else if (json_object_has_member(pt, "hr")) {
            sample.hr = (uint8_t)json_int_member(pt, "hr");
        }
        if (json_object_has_member(pt, "caloriesAndSteps")) {
            JsonObject *cs = json_object_get_object_member(pt, "caloriesAndSteps");
            if (cs != NULL) {
                sample.steps_delta = (uint16_t)json_int_member(cs, "newSteps");
            }
        } else if (json_object_has_member(pt, "steps")) {
            sample.steps_delta = (uint16_t)json_int_member(pt, "steps");
        }
        if (sample.hr > 0 || sample.steps_delta > 0) {
            g_array_append_val(summary->record_samples, sample);
        }
    }
    return summary->record_samples != NULL && summary->record_samples->len > 0;
}

static gboolean try_schema_record_versions(const uint8_t *payload,
                                           size_t payload_len,
                                           int sport_subtype,
                                           int wire_version,
                                           MiwearWorkoutSummary *summary) {
    int versions[6];
    size_t nv = 0;
    versions[nv++] = wire_version;
    for (int v = 9; v >= 3 && nv < G_N_ELEMENTS(versions); v--) {
        if (v == wire_version) {
            continue;
        }
        versions[nv++] = v;
    }
    for (size_t i = 0; i < nv; i++) {
        gchar *rel = miwear_schema_resolve_sport(sport_subtype, 0, versions[i]);
        if (rel == NULL) {
            continue;
        }
        JsonNode *decoded = NULL;
        if (decode_sport_schema(rel, payload, payload_len, &decoded)) {
            if (extract_record_points(decoded, summary)) {
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

gboolean miwear_parse_workout_record(const uint8_t *payload,
                                     size_t payload_len,
                                     int sport_subtype,
                                     int version,
                                     MiwearWorkoutSummary *summary) {
    if (payload == NULL || summary == NULL || payload_len < 16) {
        return FALSE;
    }
    if (try_schema_record_versions(payload, payload_len, sport_subtype, version, summary)) {
        return TRUE;
    }
    return FALSE;
}

static gboolean extract_gps_points(JsonNode *decoded, MiwearWorkoutSummary *summary) {
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
    summary->gps_track = g_array_new(FALSE, FALSE, sizeof(MiwearWorkoutGpsPoint));
    for (guint i = 0; i < json_array_get_length(pts); i++) {
        JsonObject *pt = json_array_get_object_element(pts, i);
        if (pt == NULL) {
            continue;
        }
        MiwearWorkoutGpsPoint gp = {0};
        if (json_object_has_member(pt, "latitude")) {
            gp.lat = json_object_get_double_member(pt, "latitude");
            gp.lon = json_object_get_double_member(pt, "longitude");
        } else if (json_object_has_member(pt, "latLng")) {
            JsonObject *ll = json_object_get_object_member(pt, "latLng");
            if (ll != NULL) {
                gp.lat = json_object_get_double_member(ll, "latitude");
                gp.lon = json_object_get_double_member(ll, "longitude");
            }
        } else if (json_object_has_member(pt, "location")) {
            JsonObject *loc = json_object_get_object_member(pt, "location");
            if (loc != NULL) {
                gp.lat = json_object_get_double_member(loc, "latitude");
                gp.lon = json_object_get_double_member(loc, "longitude");
            }
        }
        if (gp.lat != 0.0 || gp.lon != 0.0) {
            g_array_append_val(summary->gps_track, gp);
        }
    }
    return summary->gps_track != NULL && summary->gps_track->len > 0;
}

gboolean miwear_parse_workout_gps(const uint8_t *payload,
                                  size_t payload_len,
                                  int sport_subtype,
                                  int version,
                                  MiwearWorkoutSummary *summary) {
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
        gchar *rel = miwear_schema_resolve_sport(sport_subtype, 2, versions[i]);
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
