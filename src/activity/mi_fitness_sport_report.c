#include "salus/activity/mi_fitness_sport_report.h"

#include "salus/activity/mi_fitness_data.h"
#include "salus/activity/mi_fitness_report_tables.h"
#include "salus/activity/mi_fitness_validity.h"
#include "salus/activity/schema_reader.h"

#include <glib.h>
#include <json-glib/json-glib.h>
#include <string.h>

/* Report field ids (Mi Fitness SportReportBaseParser.ReportType). */
enum {
    R_START = 1, R_END = 2, R_DURATION = 3, R_VALID_DURATION = 4, R_DISTANCE = 5, R_ACTIVE_KCAL = 6,
    R_TOTAL_KCAL = 7, R_AVG_PACE = 10, R_AVG_SPEED = 11, R_STEPS = 13, R_MAX_CADENCE = 14,
    R_AVG_CADENCE = 15, R_AVG_HR = 16, R_MAX_HR = 17, R_MIN_HR = 18, R_TRAIN_EFFECT = 25,
    R_ANAEROBIC_EFFECT = 26, R_VO2MAX = 27, R_RECOVERY_H = 29, R_HR_Z5 = 30, R_HR_Z4 = 31,
    R_HR_Z3 = 32, R_HR_Z2 = 33, R_HR_Z1 = 34, R_STROKES = 35, R_MAIN_STROKE = 36, R_TURNS = 39,
    R_AVG_SWOLF = 40, R_POOL_WIDTH = 42, R_SUB_SPORT = 65, R_VO2MAX_LEVEL = 109, R_TRAINING_LOAD = 110,
    R_TRAINING_LOAD_LEVEL = 111, R_TRAINING_STATUS = 114, R_VITALITY = 117, R_AVG_STRIDE = 148,
    R_AVG_STROKE_FREQ = 157, R_HR_Z5_LOW = 158, R_HR_Z4_LOW = 159, R_HR_Z3_LOW = 160, R_HR_Z2_LOW = 161,
    R_HR_Z1_LOW = 162, R_ENTRY_TIME = 183, R_EXIT_TIME = 184, R_PLAN_ID = 206, R_PLAN_TIME = 207,
};

/* Synthetic ids for schema fields with no Mi Fitness report id: kept out of the metrics. */
#define SCHEMA_ID_UNMAPPED_BIT 2000
#define SCHEMA_ID_UNMAPPED_NOBIT (-3000)

/* ---- Tables built from a Mi Fitness schema (versions without a native table) -------------- */

typedef struct {
    MiFitnessOneDimenField *fields;
    size_t count;
    int valid_len;
} SchemaTable;

static void schema_table_free(gpointer p) {
    SchemaTable *t = p;
    g_free(t->fields);
    g_free(t);
}

static gboolean scalar_layout(const char *type, uint8_t *is_float, uint8_t *is_unsigned) {
    static const struct {
        const char *name;
        uint8_t fl, uns;
    } k[] = {
        {"uint8", 0, 1}, {"int8", 0, 0}, {"uint16", 0, 1}, {"int16", 0, 0}, {"uint32", 0, 1},
        {"int32", 0, 0}, {"uint64", 0, 1}, {"int64", 0, 0}, {"float", 1, 0}, {"float32", 1, 0},
    };
    for (size_t i = 0; i < G_N_ELEMENTS(k); i++) {
        if (g_strcmp0(type, k[i].name) == 0) {
            *is_float = k[i].fl;
            *is_unsigned = k[i].uns;
            return TRUE;
        }
    }
    return FALSE;
}

static JsonObject *member_obj(JsonObject *o, const char *key) {
    if (o == NULL || !json_object_has_member(o, key)) {
        return NULL;
    }
    JsonNode *n = json_object_get_member(o, key);
    return JSON_NODE_HOLDS_OBJECT(n) ? json_node_get_object(n) : NULL;
}

/* "/body/selectedCourse" → field name. */
static const char *body_ref_name(const char *ref) {
    return (ref != NULL && g_str_has_prefix(ref, "/body/")) ? ref + 6 : NULL;
}

/* Schema names that mean different report ids per sport: pick the one the sport's table uses. */
static int ambiguous_field_id(const char *name, int sport_type) {
    static const int target_count[] = {97, 98, 223};
    if (g_strcmp0(name, "targetCount") != 0) {
        return MI_FITNESS_FIELD_UNKNOWN;
    }
    size_t count = 0;
    const MiFitnessOneDimenField *f = mi_fitness_report_fields(sport_type, &count, NULL);
    for (size_t i = 0; i < count; i++) {
        for (size_t k = 0; k < G_N_ELEMENTS(target_count); k++) {
            if (f[i].type == target_count[k]) {
                return target_count[k];
            }
        }
    }
    return MI_FITNESS_FIELD_UNKNOWN;
}

static SchemaTable *schema_table_build(const char *schema_path, int sport_type) {
    JsonNode *root = NULL;
    if (!salus_schema_load_root(schema_path, &root)) {
        return NULL;
    }
    SchemaTable *t = NULL;
    JsonObject *structure = member_obj(json_node_get_object(root), "structure");
    JsonObject *props = member_obj(structure, "properties");
    JsonObject *body = member_obj(props, "body");
    JsonObject *flags = member_obj(member_obj(member_obj(props, "header"), "properties"), "validityFlags");
    JsonObject *bprops = member_obj(body, "properties");
    if (flags == NULL || bprops == NULL) {
        goto out;
    }

    GList *names = NULL;
    if (json_object_has_member(body, "fieldOrder")) {
        JsonArray *order = json_object_get_array_member(body, "fieldOrder");
        for (guint i = 0; i < json_array_get_length(order); i++) {
            names = g_list_append(names, (gpointer)json_array_get_string_element(order, i));
        }
    } else {
        names = json_object_get_members(bprops); /* json-glib keeps insertion order */
    }

    t = g_new0(SchemaTable, 1);
    t->valid_len = (int)json_object_get_int_member(flags, "byteSize");
    t->fields = g_new0(MiFitnessOneDimenField, g_list_length(names));
    gboolean ok = TRUE;
    size_t i = 0;
    for (GList *l = names; l != NULL && ok; l = l->next, i++) {
        const char *name = l->data;
        JsonObject *spec = member_obj(bprops, name);
        if (spec == NULL) {
            continue; /* fieldOrder can name undefined placeholders ("reserved1"): no bytes */
        }
        MiFitnessOneDimenField *f = &t->fields[t->count];
        const char *type = spec != NULL ? json_object_get_string_member(spec, "type") : NULL;
        if (type == NULL || !scalar_layout(type, &f->is_float, &f->is_unsigned)) {
            ok = FALSE;
            break;
        }
        f->byte_count = (uint8_t)json_object_get_int_member(spec, "byteSize");
        f->support_version = 0;
        f->counts_type = -1;
        /* Fields with a validity-flag reference take the next bit; constant-valid ones take none,
         * which the field walker expresses with a negative id. */
        JsonNode *valid = json_object_has_member(spec, "valid") ? json_object_get_member(spec, "valid") : NULL;
        const gboolean takes_bit = valid != NULL && JSON_NODE_HOLDS_OBJECT(valid);
        int id = mi_fitness_schema_field_id(name);
        if (id == MI_FITNESS_FIELD_UNKNOWN) {
            id = ambiguous_field_id(name, sport_type);
        }
        if (takes_bit) {
            f->type = (int16_t)(id >= 0 ? id : SCHEMA_ID_UNMAPPED_BIT + (int)i);
        } else {
            f->type = (int16_t)(id < 0 && id != MI_FITNESS_FIELD_UNKNOWN ? id : SCHEMA_ID_UNMAPPED_NOBIT - (int)i);
        }
        if (json_object_has_member(spec, "exists")) {
            JsonObject *ex = member_obj(spec, "exists");
            if (ex == NULL) {
                ok = FALSE;
                break;
            }
            const char *ref = body_ref_name(json_object_get_string_member(ex, "$data"));
            JsonArray *in = json_object_has_member(ex, "$in") ? json_object_get_array_member(ex, "$in") : NULL;
            const int dep = ref != NULL ? mi_fitness_schema_field_id(ref) : MI_FITNESS_FIELD_UNKNOWN;
            if (dep == MI_FITNESS_FIELD_UNKNOWN || in == NULL) {
                ok = FALSE;
                break;
            }
            f->has_depend = 1;
            f->depend_type = (int16_t)dep;
            for (guint k = 0; k < json_array_get_length(in) && k < 4; k++) {
                f->depend_values[f->depend_count++] = (int16_t)json_array_get_int_element(in, k);
            }
        }
        t->count++;
    }
    g_list_free(names);
    if (!ok || t->count == 0) {
        schema_table_free(t);
        t = NULL;
    }
out:
    json_node_unref(root);
    return t;
}

static const SchemaTable *schema_table_for(int sport_type, int version) {
    static GHashTable *cache; /* rel path → SchemaTable (NULL value = unusable schema) */
    gchar *rel = salus_schema_resolve_sport(sport_type, 1, version);
    if (rel == NULL) {
        return NULL;
    }
    if (cache == NULL) {
        cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, schema_table_free);
    }
    SchemaTable *t = NULL;
    if (g_hash_table_lookup_extended(cache, rel, NULL, (gpointer *)&t)) {
        g_free(rel);
        return t;
    }
    gchar *path = g_build_filename(salus_schema_dir(), rel, NULL);
    t = schema_table_build(path, sport_type);
    g_free(path);
    g_hash_table_insert(cache, rel, t);
    return t;
}

/* ---- Decoding ----------------------------------------------------------------------------- */

bool mi_fitness_sport_report_decode(const uint8_t *payload,
                                    size_t payload_len,
                                    int sport_type,
                                    int version,
                                    MiFitnessFieldValue *values,
                                    size_t *value_count,
                                    const char **source_out) {
    if (payload == NULL || values == NULL || value_count == NULL || payload_len < 12) {
        return false;
    }
    *value_count = 0;
    const size_t wire_len = payload_len - 4; /* trailing CRC32 */

    /* Mi Fitness uses the schema whenever one exists for this exact version, else the native
     * table (FitnessDataParser.parse). The two agree field-for-field wherever both exist. */
    const SchemaTable *schema = schema_table_for(sport_type, version);
    const MiFitnessOneDimenField *fields = NULL;
    size_t count = 0;
    int valid_len = -1;
    const char *source = NULL;
    if (schema != NULL) {
        fields = schema->fields;
        count = schema->count;
        valid_len = schema->valid_len;
        source = "schema";
    } else {
        fields = mi_fitness_report_fields(sport_type, &count, &source);
        SalusFitnessDataId id = {0};
        id.data_type = 1;
        id.file_type = 1;
        id.sport_type = (uint8_t)sport_type;
        id.version = (uint8_t)version;
        valid_len = mi_fitness_data_validity_len(&id);
    }
    if (fields == NULL || count == 0 || valid_len < 0) {
        return false;
    }
    const size_t header_len = 7 + 1 + (size_t)valid_len;
    if (wire_len < header_len) {
        return false;
    }
    if (source_out != NULL) {
        *source_out = source;
    }
    return mi_fitness_oneden_parse_report(fields, count, schema != NULL ? 255 : version, payload + 8,
                                          (size_t)valid_len, payload + header_len, wire_len - header_len, values,
                                          value_count);
}

/* ---- Interpretation ----------------------------------------------------------------------- */

static double value_of(const MiFitnessFieldValue *v) {
    return v->is_float ? (double)v->f32 : (double)v->i64;
}

static gboolean keep_as_metric(int id) {
    if (id < 0 || id >= SCHEMA_ID_UNMAPPED_BIT) {
        return FALSE;
    }
    return id != R_START && id != R_END && id != R_ENTRY_TIME && id != R_EXIT_TIME && id != R_PLAN_ID &&
           id != R_PLAN_TIME;
}

void mi_fitness_sport_report_apply(int sport_type,
                                   const MiFitnessFieldValue *values,
                                   size_t value_count,
                                   SalusWorkoutSummary *out) {
    const gboolean swim = sport_type == 9 || sport_type == 10;
    int active_kcal = -1, total_kcal = -1, duration = -1, valid_duration = -1;
    if (out->metrics == NULL) {
        out->metrics = g_array_new(FALSE, FALSE, sizeof(SalusWorkoutMetric));
    }
    for (size_t i = 0; i < value_count; i++) {
        const MiFitnessFieldValue *v = &values[i];
        if (!v->valid) {
            continue;
        }
        const double d = value_of(v);
        const int n = (int)v->i64;
        switch (v->type) {
            case R_START: out->start_sec = (uint32_t)v->i64; break;
            case R_END: out->end_sec = (uint32_t)v->i64; break;
            case R_DURATION: duration = n; break;
            case R_VALID_DURATION: valid_duration = n; break;
            case R_DISTANCE: out->distance_m = n; break;
            case R_ACTIVE_KCAL: active_kcal = n; break;
            case R_TOTAL_KCAL: total_kcal = n; break;
            case R_AVG_HR: out->hr_avg = n; break;
            case R_MAX_HR: out->hr_max = n; break;
            case R_MIN_HR: out->hr_min = n; break;
            case R_SUB_SPORT: if (n > 0) out->sub_sport_type = n; break;
            case R_AVG_PACE:
                if (swim) out->swim_avg_pace_sec_per_100m = n;
                else out->walk_avg_pace_sec_per_km = n;
                break;
            case R_AVG_SPEED: out->walk_avg_speed_kmh = (float)d; break;
            case R_STEPS: out->walk_steps = n; break;
            case R_AVG_CADENCE: out->walk_avg_cadence_spm = n; break;
            case R_MAX_CADENCE: out->walk_max_cadence_spm = n; break;
            case R_AVG_STRIDE: out->walk_step_length_cm = n; break;
            case R_TRAIN_EFFECT:
                out->walk_te_aerobic = (float)d;
                out->swim_training_effect = (float)d;
                break;
            case R_ANAEROBIC_EFFECT: out->walk_te_anaerobic = (float)d; break;
            case R_VO2MAX: out->vo2_max = n; break;
            case R_VO2MAX_LEVEL: out->vo2_max_level = n; break;
            case R_RECOVERY_H: out->recovery_min = n * 60; break;
            case R_TRAINING_LOAD: out->training_load = n; break;
            case R_TRAINING_LOAD_LEVEL: out->training_load_level = n; break;
            case R_TRAINING_STATUS: if (n > 0) out->training_status = n; break;
            case R_VITALITY: out->walk_vitality_points = out->swim_vitality_points = n; break;
            case R_HR_Z1: out->hr_zones.zone1_sec = n; break;
            case R_HR_Z2: out->hr_zones.zone2_sec = n; break;
            case R_HR_Z3: out->hr_zones.zone3_sec = n; break;
            case R_HR_Z4: out->hr_zones.zone4_sec = n; break;
            case R_HR_Z5: out->hr_zones.zone5_sec = n; break;
            case R_HR_Z1_LOW: out->hr_zones.zone1_lower_bpm = n; break;
            case R_HR_Z2_LOW: out->hr_zones.zone2_lower_bpm = n; break;
            case R_HR_Z3_LOW: out->hr_zones.zone3_lower_bpm = n; break;
            case R_HR_Z4_LOW: out->hr_zones.zone4_lower_bpm = n; break;
            case R_HR_Z5_LOW: out->hr_zones.zone5_lower_bpm = n; break;
            case R_STROKES: out->swim_total_strokes = n; break;
            case R_MAIN_STROKE: out->swim_main_stroke = n; break;
            case R_TURNS: out->swim_lengths = n; break;
            case R_AVG_SWOLF: out->swim_avg_swolf = n; break;
            case R_POOL_WIDTH: out->swim_pool_width_m = n; break;
            case R_AVG_STROKE_FREQ: out->swim_avg_spm = n; break;
            default: break;
        }
        if (keep_as_metric(v->type)) {
            const SalusWorkoutMetric m = {v->type, d};
            g_array_append_val(out->metrics, m);
        }
    }
    out->duration_sec = duration > 0 ? duration : valid_duration > 0 ? valid_duration : out->duration_sec;
    out->calories = total_kcal > 0 ? total_kcal : active_kcal > 0 ? active_kcal : out->calories;
    out->walk_training_load = out->swim_training_load = out->training_load;
    out->walk_recovery_min = out->swim_recovery_min = out->recovery_min;
    if (swim) {
        out->has_swim_detail = TRUE;
    } else if (out->walk_steps > 0 || out->walk_avg_pace_sec_per_km > 0) {
        out->has_walk_detail = TRUE;
        if (out->walk_avg_pace_sec_per_km <= 0 && out->distance_m > 0 && out->duration_sec > 0) {
            out->walk_avg_pace_sec_per_km = (int)((uint64_t)out->duration_sec * 1000u / (uint32_t)out->distance_m);
        }
    }
}

bool mi_fitness_parse_sport_report(const uint8_t *payload,
                                   size_t payload_len,
                                   int sport_type,
                                   int version,
                                   SalusWorkoutSummary *out) {
    MiFitnessFieldValue values[MI_FITNESS_FIELD_VALUE_MAX];
    size_t n = 0;
    if (out == NULL || !mi_fitness_sport_report_decode(payload, payload_len, sport_type, version, values, &n, NULL) ||
        n == 0) {
        return false;
    }
    mi_fitness_sport_report_apply(sport_type, values, n, out);
    return out->duration_sec > 0 || out->distance_m > 0 || out->calories > 0;
}
