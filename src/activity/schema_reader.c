#include "miwear/activity/schema_reader.h"
#include "miwear/activity/mi_fitness_data.h"
#include "miwear/util.h"

#include <math.h>
#include <string.h>

#ifndef MIWEAR_SCHEMA_DIR
#define MIWEAR_SCHEMA_DIR "third_party/mi-fitness-schemas"
#endif

typedef struct {
    const uint8_t *data;
    size_t off;
    size_t len;
} SchemaCursor;

typedef struct {
    const JsonNode *schema_root;
    GError **error;
} SchemaDecodeCtx;

static const struct {
    int sport;
    const char *activity;
} k_sport_activity[] = {
    {3, "indoor_running"},   {7, "indoor_cycling"},   {8, "free_training"},
    {9, "swimming"},         {10, "swimming"},        {11, "elliptical_training"},
    {12, "yoga"},            {13, "rowing_workout"},  {14, "rope_skipping"},
    {16, "hiit"},            {17, "triathlon"},       {18, "ball_games"},
    {19, "basketball"},      {20, "golf"},            {21, "skiing"},
    {22, "outdoor_foot"},    {23, "outdoor_non_foot"},{24, "climbing"},
    {25, "diving"},          {26, "esports"},
};

static const char *k_file_type_sport[] = {"record", "report", "gps", "hrr"};
static const char *k_file_type_health[] = {"record", "report"};

static const char *daily_schema_activity(int daily_type) {
    if (daily_type == 0) {
        return "daily_activity";
    }
    if (daily_type == 6) {
        return "manual_measurement";
    }
    switch (daily_type) {
        case 2:
            return "daytime_sleep";
        case 3:
            return "night_sleep";
        case 8:
            return "full_day_sleep";
        case 9:
            return "abnormal";
        case 10:
            return "weight_fat";
        case 11:
            return "ecg";
        case 12:
            return "temperature";
        default:
            return NULL;
    }
}

static gchar *miwear_schema_resolve_activity_file(const char *activity, const char *file_type, int version) {
    if (activity == NULL) {
        return NULL;
    }
    gchar *index_path = g_build_filename(miwear_schema_dir(), "index.json", NULL);
    GError *err = NULL;
    JsonParser *parser = json_parser_new();
    if (!json_parser_load_from_file(parser, index_path, &err)) {
        g_free(index_path);
        g_clear_error(&err);
        g_object_unref(parser);
        return NULL;
    }
    g_free(index_path);
    JsonObject *index = json_node_get_object(json_parser_get_root(parser));
    if (!json_object_has_member(index, activity)) {
        g_object_unref(parser);
        return NULL;
    }
    JsonObject *act = json_object_get_object_member(index, activity);
    if (!json_object_has_member(act, file_type)) {
        g_object_unref(parser);
        return NULL;
    }
    JsonObject *vers = json_object_get_object_member(act, file_type);
    gchar *ver_key = g_strdup_printf("v%d", version);
    if (!json_object_has_member(vers, ver_key)) {
        g_free(ver_key);
        g_object_unref(parser);
        return NULL;
    }
    const char *rel = json_object_get_string_member(vers, ver_key);
    gchar *result = g_strdup(rel);
    g_free(ver_key);
    g_object_unref(parser);
    return result;
}

const char *miwear_schema_dir(void) {
    const char *env = g_getenv("MIWEAR_SCHEMA_DIR");
    return env != NULL && env[0] != '\0' ? env : MIWEAR_SCHEMA_DIR;
}

static gboolean cursor_need(SchemaCursor *c, size_t n) {
    return c->off + n <= c->len;
}

static gboolean cursor_read(SchemaCursor *c, uint8_t *out, size_t n) {
    if (!cursor_need(c, n)) {
        return FALSE;
    }
    memcpy(out, c->data + c->off, n);
    c->off += n;
    return TRUE;
}

static gboolean cursor_u8(SchemaCursor *c, uint8_t *v) {
    return cursor_read(c, v, 1);
}

static gboolean cursor_u16(SchemaCursor *c, uint16_t *v) {
    uint8_t b[2];
    if (!cursor_read(c, b, 2)) {
        return FALSE;
    }
    *v = miwear_read_u16_le(b);
    return TRUE;
}

static gboolean cursor_u32(SchemaCursor *c, uint32_t *v) {
    uint8_t b[4];
    if (!cursor_read(c, b, 4)) {
        return FALSE;
    }
    *v = miwear_read_u32_le(b);
    return TRUE;
}

static gboolean cursor_i32(SchemaCursor *c, int32_t *v) {
    uint32_t u;
    if (!cursor_u32(c, &u)) {
        return FALSE;
    }
    *v = (int32_t)u;
    return TRUE;
}

static gboolean cursor_f32(SchemaCursor *c, double *v) {
    uint8_t b[4];
    if (!cursor_read(c, b, 4)) {
        return FALSE;
    }
    float f;
    uint32_t raw = miwear_read_u32_le(b);
    memcpy(&f, &raw, sizeof(f));
    if (!isfinite((double)f)) {
        *v = 0.0;
    } else {
        *v = (double)f;
    }
    return TRUE;
}

GBytes *miwear_schema_sport_wire_buffer(const uint8_t *payload, size_t payload_len) {
    if (payload == NULL || payload_len < 8) {
        return NULL;
    }
    const size_t tail_len = payload_len - 8;
    GByteArray *arr = g_byte_array_sized_new(7 + 1 + tail_len);
    g_byte_array_append(arr, payload, 7);
    g_byte_array_append(arr, (const uint8_t *)"\x00", 1);
    if (tail_len > 0) {
        g_byte_array_append(arr, payload + 8, (guint)tail_len);
    }
    return g_byte_array_free_to_bytes(arr);
}

static JsonObject *resolve_ref(const SchemaDecodeCtx *ctx, JsonObject *node) {
    if (!json_object_has_member(node, "$ref")) {
        return node;
    }
    const char *ref = json_object_get_string_member(node, "$ref");
    if (ref == NULL || strncmp(ref, "#/$defs/", 9) != 0) {
        return node;
    }
    const char *name = ref + 9;
    JsonObject *root = json_node_get_object((JsonNode *)ctx->schema_root);
    if (!json_object_has_member(root, "$defs")) {
        return node;
    }
    JsonObject *defs = json_object_get_object_member(root, "$defs");
    if (!json_object_has_member(defs, name)) {
        return node;
    }
    return json_object_get_object_member(defs, name);
}

static gboolean read_bitfield(SchemaCursor *c, JsonObject *spec, JsonBuilder *b) {
    int byte_size = 1;
    if (json_object_has_member(spec, "byteSize")) {
        byte_size = json_object_get_int_member(spec, "byteSize");
    }
    if (byte_size <= 0 || byte_size > 16) {
        return FALSE;
    }
    uint8_t raw_bytes[16];
    if (!cursor_read(c, raw_bytes, (size_t)byte_size)) {
        return FALSE;
    }
    uint64_t raw = 0;
    for (int i = 0; i < byte_size; i++) {
        raw |= (uint64_t)raw_bytes[i] << (8 * i);
    }
    JsonObject *props = json_object_get_object_member(spec, "properties");
    JsonArray *order = NULL;
    if (json_object_has_member(spec, "fieldOrder")) {
        order = json_object_get_array_member(spec, "fieldOrder");
    }
    json_builder_begin_object(b);
    if (order != NULL) {
        const guint n = json_array_get_length(order);
        for (guint i = 0; i < n; i++) {
            const char *fname = json_array_get_string_element(order, i);
            if (fname == NULL || !json_object_has_member(props, fname)) {
                continue;
            }
            JsonObject *bit = json_object_get_object_member(props, fname);
            int start = json_object_get_int_member(bit, "startBit");
            int end = json_object_get_int_member(bit, "endBit");
            int width = end - start + 1;
            uint64_t mask = width >= 64 ? UINT64_MAX : ((uint64_t)1 << width) - 1;
            uint64_t val = (raw >> start) & mask;
            json_builder_set_member_name(b, fname);
            json_builder_add_int_value(b, (gint64)val);
        }
    }
    json_builder_end_object(b);
    return TRUE;
}

static gboolean decode_field(SchemaDecodeCtx *ctx, SchemaCursor *c, JsonObject *spec, JsonBuilder *b);

static gboolean decode_object(SchemaDecodeCtx *ctx, SchemaCursor *c, JsonObject *spec, JsonBuilder *b) {
    JsonObject *props = json_object_get_object_member(spec, "properties");
    JsonArray *order = NULL;
    if (json_object_has_member(spec, "fieldOrder")) {
        order = json_object_get_array_member(spec, "fieldOrder");
    }
    json_builder_begin_object(b);
    if (order != NULL) {
        const guint n = json_array_get_length(order);
        for (guint i = 0; i < n; i++) {
            const char *name = json_array_get_string_element(order, i);
            if (name == NULL || !json_object_has_member(props, name)) {
                continue;
            }
            JsonObject *field = json_object_get_object_member(props, name);
            json_builder_set_member_name(b, name);
            if (!decode_field(ctx, c, field, b)) {
                return FALSE;
            }
        }
    }
    json_builder_end_object(b);
    return TRUE;
}

static int array_fixed_count(JsonObject *spec) {
    if (!json_object_has_member(spec, "itemCount")) {
        return 1;
    }
    JsonNode *ic_node = json_object_get_member(spec, "itemCount");
    if (JSON_NODE_HOLDS_VALUE(ic_node)) {
        GType t = json_node_get_value_type(ic_node);
        if (t == G_TYPE_STRING) {
            const char *ic = json_node_get_string(ic_node);
            if (ic != NULL && g_strcmp0(ic, "EOF") == 0) {
                return -1;
            }
        } else if (t == G_TYPE_INT64) {
            return (int)json_node_get_int(ic_node);
        }
    } else if (json_node_get_node_type(ic_node) == JSON_NODE_OBJECT) {
        return -2;
    }
    return -2;
}

static gboolean decode_array(SchemaDecodeCtx *ctx, SchemaCursor *c, JsonObject *spec, JsonBuilder *b) {
    int count = array_fixed_count(spec);
    if (count == -2) {
        return FALSE;
    }
    JsonObject *items = json_object_get_object_member(spec, "items");
    items = resolve_ref(ctx, items);
    json_builder_begin_array(b);
    if (count == -1) {
        while (c->off < c->len) {
            if (!decode_field(ctx, c, items, b)) {
                break;
            }
        }
    } else {
        for (int i = 0; i < count; i++) {
            if (c->off >= c->len) {
                break;
            }
            if (!decode_field(ctx, c, items, b)) {
                return FALSE;
            }
        }
    }
    json_builder_end_array(b);
    return TRUE;
}

static gboolean decode_scalar(SchemaCursor *c, const char *typ, JsonBuilder *b) {
    if (g_strcmp0(typ, "uint8") == 0) {
        uint8_t v;
        if (!cursor_u8(c, &v)) {
            return FALSE;
        }
        json_builder_add_int_value(b, v);
        return TRUE;
    }
    if (g_strcmp0(typ, "int8") == 0) {
        uint8_t v;
        if (!cursor_u8(c, &v)) {
            return FALSE;
        }
        json_builder_add_int_value(b, v >= 128 ? v - 256 : v);
        return TRUE;
    }
    if (g_strcmp0(typ, "uint16") == 0) {
        uint16_t v;
        if (!cursor_u16(c, &v)) {
            return FALSE;
        }
        json_builder_add_int_value(b, v);
        return TRUE;
    }
    if (g_strcmp0(typ, "uint32") == 0) {
        uint32_t v;
        if (!cursor_u32(c, &v)) {
            return FALSE;
        }
        json_builder_add_int_value(b, v);
        return TRUE;
    }
    if (g_strcmp0(typ, "int32") == 0) {
        int32_t v;
        if (!cursor_i32(c, &v)) {
            return FALSE;
        }
        json_builder_add_int_value(b, v);
        return TRUE;
    }
    if (g_strcmp0(typ, "float") == 0 || g_strcmp0(typ, "float32") == 0) {
        double v;
        if (!cursor_f32(c, &v)) {
            return FALSE;
        }
        json_builder_add_double_value(b, v);
        return TRUE;
    }
    return FALSE;
}

static gboolean decode_field(SchemaDecodeCtx *ctx, SchemaCursor *c, JsonObject *spec, JsonBuilder *b) {
    spec = resolve_ref(ctx, spec);
    const char *typ = json_object_get_string_member(spec, "type");
    if (typ == NULL) {
        return FALSE;
    }
    if (g_strcmp0(typ, "bitField") == 0) {
        return read_bitfield(c, spec, b);
    }
    if (g_strcmp0(typ, "object") == 0) {
        return decode_object(ctx, c, spec, b);
    }
    if (g_strcmp0(typ, "array") == 0) {
        return decode_array(ctx, c, spec, b);
    }
    if (g_strcmp0(typ, "binary") == 0) {
        size_t n = 0;
        if (json_object_has_member(spec, "byteSize")) {
            JsonNode *bs = json_object_get_member(spec, "byteSize");
            if (json_node_get_value_type(bs) == G_TYPE_STRING) {
                const char *s = json_node_get_string(bs);
                if (s != NULL && g_strcmp0(s, "EOF") == 0) {
                    n = c->len - c->off;
                }
            } else {
                n = (size_t)json_object_get_int_member(spec, "byteSize");
            }
        }
        if (n == 0 || !cursor_need(c, n)) {
            json_builder_add_string_value(b, "");
            return TRUE;
        }
        gchar *b64 = g_base64_encode(c->data + c->off, (gsize)n);
        c->off += n;
        json_builder_add_string_value(b, b64 != NULL ? b64 : "");
        g_free(b64);
        return TRUE;
    }
    return decode_scalar(c, typ, b);
}

gboolean miwear_schema_decode(const JsonNode *schema_root,
                              const uint8_t *buffer,
                              size_t buffer_len,
                              JsonNode **out_decoded) {
    if (schema_root == NULL || buffer == NULL || out_decoded == NULL ||
        json_node_get_node_type((JsonNode *)schema_root) != JSON_NODE_OBJECT) {
        return FALSE;
    }
    JsonObject *root_obj = json_node_get_object((JsonNode *)schema_root);
    if (!json_object_has_member(root_obj, "structure")) {
        return FALSE;
    }
    SchemaDecodeCtx ctx = {.schema_root = schema_root, .error = NULL};
    SchemaCursor cur = {.data = buffer, .off = 0, .len = buffer_len};
    JsonBuilder *builder = json_builder_new();
    JsonObject *structure = json_object_get_object_member(root_obj, "structure");
    if (!decode_field(&ctx, &cur, structure, builder)) {
        g_object_unref(builder);
        return FALSE;
    }
    *out_decoded = json_builder_get_root(builder);
    g_object_unref(builder);
    return TRUE;
}

gboolean miwear_schema_load_root(const char *schema_json_path, JsonNode **out_root) {
    if (schema_json_path == NULL || out_root == NULL) {
        return FALSE;
    }
    GError *err = NULL;
    if (!g_file_test(schema_json_path, G_FILE_TEST_EXISTS)) {
        return FALSE;
    }
    JsonParser *parser = json_parser_new();
    if (!json_parser_load_from_file(parser, schema_json_path, &err)) {
        g_clear_error(&err);
        g_object_unref(parser);
        return FALSE;
    }
    *out_root = json_node_copy(json_parser_get_root(parser));
    g_object_unref(parser);
    return TRUE;
}

gchar *miwear_schema_resolve_sport(int sport_type, int file_type, int version) {
    const char *activity = NULL;
    for (size_t i = 0; i < G_N_ELEMENTS(k_sport_activity); i++) {
        if (k_sport_activity[i].sport == sport_type) {
            activity = k_sport_activity[i].activity;
            break;
        }
    }
    if (activity == NULL || file_type < 0 || file_type > 3) {
        return NULL;
    }
    return miwear_schema_resolve_activity_file(activity, k_file_type_sport[file_type], version);
}

gchar *miwear_schema_resolve_daily(int daily_type, int file_type, int version) {
    if (file_type < 0 || file_type > 1) {
        return NULL;
    }
    daily_type = mi_fitness_apk_daily_type(daily_type);
    const char *activity = daily_schema_activity(daily_type);
    if (activity == NULL) {
        return NULL;
    }
    return miwear_schema_resolve_activity_file(activity, k_file_type_health[file_type], version);
}

void miwear_schema_maybe_capture(const uint8_t *payload,
                                 size_t payload_len,
                                 int sport_type,
                                 int detail,
                                 int version,
                                 uint32_t file_ts) {
    const char *dir = g_getenv("MIWEAR_CAPTURE_DIR");
    if (dir == NULL || dir[0] == '\0' || payload == NULL || payload_len == 0) {
        return;
    }
    g_mkdir_with_parents(dir, 0755);
    gchar *base = g_strdup_printf("%s/sport_%d_detail%d_v%d_%u",
                                  dir,
                                  sport_type,
                                  detail,
                                  version,
                                  (unsigned)file_ts);
    gchar *bin_path = g_strdup_printf("%s.bin", base);
    gchar *meta_path = g_strdup_printf("%s.json", base);
    g_file_set_contents(bin_path, (const char *)payload, (gssize)payload_len, NULL);
    JsonBuilder *b = json_builder_new();
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "sport_type");
    json_builder_add_int_value(b, sport_type);
    json_builder_set_member_name(b, "detail");
    json_builder_add_int_value(b, detail);
    json_builder_set_member_name(b, "version");
    json_builder_add_int_value(b, version);
    json_builder_set_member_name(b, "file_ts");
    json_builder_add_int_value(b, file_ts);
    json_builder_set_member_name(b, "size");
    json_builder_add_int_value(b, (gint64)payload_len);
    json_builder_end_object(b);
    JsonNode *meta = json_builder_get_root(b);
    JsonGenerator *gen = json_generator_new();
    json_generator_set_root(gen, meta);
    json_generator_to_file(gen, meta_path, NULL);
    g_object_unref(gen);
    json_node_unref(meta);
    g_object_unref(b);
    g_free(bin_path);
    g_free(meta_path);
    g_free(base);
}
