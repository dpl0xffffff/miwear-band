#include "miwear/db/workout_store.h"

#include "miwear/activity/workout_record.h"

#include <json-glib/json-glib.h>

static void json_add_hr_zones(JsonBuilder *b, const MiwearWorkoutHrZones *z) {
    if (b == NULL || z == NULL) {
        return;
    }
    json_builder_set_member_name(b, "hr_zones");
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "zone1_sec");
    json_builder_add_int_value(b, z->zone1_sec);
    json_builder_set_member_name(b, "zone2_sec");
    json_builder_add_int_value(b, z->zone2_sec);
    json_builder_set_member_name(b, "zone3_sec");
    json_builder_add_int_value(b, z->zone3_sec);
    json_builder_set_member_name(b, "zone4_sec");
    json_builder_add_int_value(b, z->zone4_sec);
    json_builder_set_member_name(b, "zone5_sec");
    json_builder_add_int_value(b, z->zone5_sec);
    json_builder_set_member_name(b, "zone1_lower_bpm");
    json_builder_add_int_value(b, z->zone1_lower_bpm);
    json_builder_set_member_name(b, "zone2_lower_bpm");
    json_builder_add_int_value(b, z->zone2_lower_bpm);
    json_builder_set_member_name(b, "zone3_lower_bpm");
    json_builder_add_int_value(b, z->zone3_lower_bpm);
    json_builder_set_member_name(b, "zone4_lower_bpm");
    json_builder_add_int_value(b, z->zone4_lower_bpm);
    json_builder_set_member_name(b, "zone5_lower_bpm");
    json_builder_add_int_value(b, z->zone5_lower_bpm);
    json_builder_end_object(b);
}

static void json_add_series(JsonBuilder *b, const MiwearWorkoutSummary *summary) {
    json_builder_set_member_name(b, "record_interval_sec");
    json_builder_add_int_value(b, summary->record_interval_sec);
    json_builder_set_member_name(b, "record_samples");
    json_builder_begin_array(b);
    if (summary->record_samples != NULL) {
        for (guint i = 0; i < summary->record_samples->len; i++) {
            const MiwearWorkoutSample *s = &g_array_index(summary->record_samples, MiwearWorkoutSample, i);
            json_builder_begin_array(b);
            json_builder_add_int_value(b, (gint64)s->offset_sec);
            json_builder_add_int_value(b, (gint64)s->hr);
            json_builder_add_int_value(b, (gint64)s->steps_delta);
            json_builder_end_array(b);
        }
    }
    json_builder_end_array(b);
    json_builder_set_member_name(b, "gps_track");
    json_builder_begin_array(b);
    if (summary->gps_track != NULL) {
        for (guint i = 0; i < summary->gps_track->len; i++) {
            const MiwearWorkoutGpsPoint *p = &g_array_index(summary->gps_track, MiwearWorkoutGpsPoint, i);
            json_builder_begin_array(b);
            json_builder_add_double_value(b, p->lat);
            json_builder_add_double_value(b, p->lon);
            json_builder_end_array(b);
        }
    }
    json_builder_end_array(b);
}

static void read_series(JsonObject *o, MiwearWorkoutSummary *summary) {
    miwear_workout_summary_free_series(summary);
    if (json_object_has_member(o, "record_interval_sec")) {
        summary->record_interval_sec = (int)json_object_get_int_member(o, "record_interval_sec");
    }
    if (json_object_has_member(o, "record_samples")) {
        JsonArray *arr = json_object_get_array_member(o, "record_samples");
        if (arr != NULL && json_array_get_length(arr) > 0) {
            summary->record_samples = g_array_new(FALSE, FALSE, sizeof(MiwearWorkoutSample));
            for (guint i = 0; i < json_array_get_length(arr); i++) {
                JsonArray *row = json_array_get_array_element(arr, i);
                if (row == NULL || json_array_get_length(row) < 3) {
                    continue;
                }
                MiwearWorkoutSample s = {0};
                s.offset_sec = (uint32_t)json_array_get_int_element(row, 0);
                s.hr = (uint8_t)json_array_get_int_element(row, 1);
                s.steps_delta = (uint16_t)json_array_get_int_element(row, 2);
                g_array_append_val(summary->record_samples, s);
            }
        }
    }
    if (json_object_has_member(o, "gps_track")) {
        JsonArray *arr = json_object_get_array_member(o, "gps_track");
        if (arr != NULL && json_array_get_length(arr) > 0) {
            summary->gps_track = g_array_new(FALSE, FALSE, sizeof(MiwearWorkoutGpsPoint));
            for (guint i = 0; i < json_array_get_length(arr); i++) {
                JsonArray *row = json_array_get_array_element(arr, i);
                if (row == NULL || json_array_get_length(row) < 2) {
                    continue;
                }
                MiwearWorkoutGpsPoint p = {0};
                p.lat = json_array_get_double_element(row, 0);
                p.lon = json_array_get_double_element(row, 1);
                g_array_append_val(summary->gps_track, p);
            }
        }
    }
}

static void read_hr_zones(JsonObject *o, MiwearWorkoutHrZones *z) {
    if (o == NULL || z == NULL || !json_object_has_member(o, "hr_zones")) {
        return;
    }
    JsonObject *hz = json_object_get_object_member(o, "hr_zones");
    if (hz == NULL) {
        return;
    }
    z->zone1_sec = (int)json_object_get_int_member(hz, "zone1_sec");
    z->zone2_sec = (int)json_object_get_int_member(hz, "zone2_sec");
    z->zone3_sec = (int)json_object_get_int_member(hz, "zone3_sec");
    z->zone4_sec = (int)json_object_get_int_member(hz, "zone4_sec");
    z->zone5_sec = (int)json_object_get_int_member(hz, "zone5_sec");
    z->zone1_lower_bpm = (int)json_object_get_int_member(hz, "zone1_lower_bpm");
    z->zone2_lower_bpm = (int)json_object_get_int_member(hz, "zone2_lower_bpm");
    z->zone3_lower_bpm = (int)json_object_get_int_member(hz, "zone3_lower_bpm");
    z->zone4_lower_bpm = (int)json_object_get_int_member(hz, "zone4_lower_bpm");
    z->zone5_lower_bpm = (int)json_object_get_int_member(hz, "zone5_lower_bpm");
}

gchar *miwear_workout_summary_to_json(int sport_type, const MiwearWorkoutSummary *summary) {
    if (summary == NULL) {
        return NULL;
    }
    JsonBuilder *b = json_builder_new();
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "sport_type");
    json_builder_add_int_value(b, sport_type);
    json_builder_set_member_name(b, "file_ts_sec");
    json_builder_add_int_value(b, summary->file_ts_sec);
    json_builder_set_member_name(b, "start_sec");
    json_builder_add_int_value(b, summary->start_sec);
    json_builder_set_member_name(b, "end_sec");
    json_builder_add_int_value(b, summary->end_sec);
    json_builder_set_member_name(b, "duration_sec");
    json_builder_add_int_value(b, summary->duration_sec);
    json_builder_set_member_name(b, "distance_m");
    json_builder_add_int_value(b, summary->distance_m);
    json_builder_set_member_name(b, "calories");
    json_builder_add_int_value(b, summary->calories);
    json_builder_set_member_name(b, "hr_avg");
    json_builder_add_int_value(b, summary->hr_avg);
    json_builder_set_member_name(b, "hr_max");
    json_builder_add_int_value(b, summary->hr_max);
    json_builder_set_member_name(b, "hr_min");
    json_builder_add_int_value(b, summary->hr_min);
    json_builder_set_member_name(b, "sport_subtype");
    json_builder_add_int_value(b, summary->sport_subtype);
    json_builder_set_member_name(b, "version");
    json_builder_add_int_value(b, summary->version);
    json_builder_set_member_name(b, "sport_name");
    json_builder_add_string_value(b, summary->sport_name != NULL ? summary->sport_name : "");
    json_builder_set_member_name(b, "has_swim_detail");
    json_builder_add_boolean_value(b, summary->has_swim_detail);
    json_builder_set_member_name(b, "swim_lengths");
    json_builder_add_int_value(b, summary->swim_lengths);
    json_builder_set_member_name(b, "swim_main_stroke");
    json_builder_add_int_value(b, summary->swim_main_stroke);
    json_builder_set_member_name(b, "swim_avg_pace_sec_per_100m");
    json_builder_add_int_value(b, summary->swim_avg_pace_sec_per_100m);
    json_builder_set_member_name(b, "swim_avg_swolf");
    json_builder_add_int_value(b, summary->swim_avg_swolf);
    json_builder_set_member_name(b, "swim_total_strokes");
    json_builder_add_int_value(b, summary->swim_total_strokes);
    json_builder_set_member_name(b, "swim_avg_spm");
    json_builder_add_int_value(b, summary->swim_avg_spm);
    json_builder_set_member_name(b, "swim_training_load");
    json_builder_add_int_value(b, summary->swim_training_load);
    json_builder_set_member_name(b, "swim_recovery_min");
    json_builder_add_int_value(b, summary->swim_recovery_min);
    json_builder_set_member_name(b, "swim_vitality_points");
    json_builder_add_int_value(b, summary->swim_vitality_points);
    json_builder_set_member_name(b, "swim_pool_width_m");
    json_builder_add_int_value(b, summary->swim_pool_width_m);
    json_builder_set_member_name(b, "swim_training_effect");
    json_builder_add_double_value(b, (gdouble)summary->swim_training_effect);
    json_builder_set_member_name(b, "has_walk_detail");
    json_builder_add_boolean_value(b, summary->has_walk_detail);
    json_builder_set_member_name(b, "walk_steps");
    json_builder_add_int_value(b, summary->walk_steps);
    json_builder_set_member_name(b, "walk_avg_pace_sec_per_km");
    json_builder_add_int_value(b, summary->walk_avg_pace_sec_per_km);
    json_builder_set_member_name(b, "walk_avg_speed_kmh");
    json_builder_add_double_value(b, (gdouble)summary->walk_avg_speed_kmh);
    json_builder_set_member_name(b, "walk_step_length_cm");
    json_builder_add_int_value(b, summary->walk_step_length_cm);
    json_builder_set_member_name(b, "walk_avg_cadence_spm");
    json_builder_add_int_value(b, summary->walk_avg_cadence_spm);
    json_builder_set_member_name(b, "walk_max_cadence_spm");
    json_builder_add_int_value(b, summary->walk_max_cadence_spm);
    json_builder_set_member_name(b, "walk_te_aerobic");
    json_builder_add_double_value(b, (gdouble)summary->walk_te_aerobic);
    json_builder_set_member_name(b, "walk_te_anaerobic");
    json_builder_add_double_value(b, (gdouble)summary->walk_te_anaerobic);
    json_builder_set_member_name(b, "walk_training_load");
    json_builder_add_int_value(b, summary->walk_training_load);
    json_builder_set_member_name(b, "walk_recovery_min");
    json_builder_add_int_value(b, summary->walk_recovery_min);
    json_builder_set_member_name(b, "walk_vitality_points");
    json_builder_add_int_value(b, summary->walk_vitality_points);
    json_add_hr_zones(b, &summary->hr_zones);
    json_add_series(b, summary);
    json_builder_end_object(b);

    JsonNode *root = json_builder_get_root(b);
    JsonGenerator *gen = json_generator_new();
    json_generator_set_root(gen, root);
    gchar *text = json_generator_to_data(gen, NULL);
    g_object_unref(gen);
    json_node_unref(root);
    g_object_unref(b);
    return text;
}

gboolean miwear_workout_summary_from_json(const char *json,
                                          int *sport_type_out,
                                          MiwearWorkoutSummary *summary_out) {
    if (json == NULL || summary_out == NULL) {
        return FALSE;
    }
    JsonParser *parser = json_parser_new();
    GError *err = NULL;
    if (!json_parser_load_from_data(parser, json, -1, &err)) {
        g_warning("workout JSON parse: %s", err != NULL ? err->message : "unknown");
        g_clear_error(&err);
        g_object_unref(parser);
        return FALSE;
    }
    JsonNode *root = json_parser_get_root(parser);
    if (root == NULL || json_node_get_node_type(root) != JSON_NODE_OBJECT) {
        g_object_unref(parser);
        return FALSE;
    }
    JsonObject *o = json_node_get_object(root);
    if (sport_type_out != NULL) {
        *sport_type_out = json_object_has_member(o, "sport_type") ? (int)json_object_get_int_member(o, "sport_type") : 1;
    }
    summary_out->file_ts_sec = (uint32_t)json_object_get_int_member(o, "file_ts_sec");
    summary_out->start_sec = (uint32_t)json_object_get_int_member(o, "start_sec");
    summary_out->end_sec = (uint32_t)json_object_get_int_member(o, "end_sec");
    summary_out->duration_sec = (int)json_object_get_int_member(o, "duration_sec");
    summary_out->distance_m = (int)json_object_get_int_member(o, "distance_m");
    summary_out->calories = (int)json_object_get_int_member(o, "calories");
    summary_out->hr_avg = (int)json_object_get_int_member(o, "hr_avg");
    summary_out->hr_max = (int)json_object_get_int_member(o, "hr_max");
    summary_out->hr_min = (int)json_object_get_int_member(o, "hr_min");
    summary_out->sport_subtype = (int)json_object_get_int_member(o, "sport_subtype");
    summary_out->version = (int)json_object_get_int_member(o, "version");
    if (json_object_has_member(o, "sport_name")) {
        const char *nm = json_object_get_string_member(o, "sport_name");
        summary_out->sport_name = g_strdup(nm != NULL ? nm : "");
    }
    summary_out->has_swim_detail =
        json_object_has_member(o, "has_swim_detail") && json_object_get_boolean_member(o, "has_swim_detail");
    summary_out->swim_lengths = (int)json_object_get_int_member(o, "swim_lengths");
    summary_out->swim_main_stroke = (int)json_object_get_int_member(o, "swim_main_stroke");
    summary_out->swim_avg_pace_sec_per_100m = (int)json_object_get_int_member(o, "swim_avg_pace_sec_per_100m");
    summary_out->swim_avg_swolf = (int)json_object_get_int_member(o, "swim_avg_swolf");
    summary_out->swim_total_strokes = (int)json_object_get_int_member(o, "swim_total_strokes");
    summary_out->swim_avg_spm = (int)json_object_get_int_member(o, "swim_avg_spm");
    summary_out->swim_training_load = (int)json_object_get_int_member(o, "swim_training_load");
    summary_out->swim_recovery_min = (int)json_object_get_int_member(o, "swim_recovery_min");
    summary_out->swim_vitality_points = (int)json_object_get_int_member(o, "swim_vitality_points");
    summary_out->swim_pool_width_m = (int)json_object_get_int_member(o, "swim_pool_width_m");
    summary_out->swim_training_effect = (float)json_object_get_double_member(o, "swim_training_effect");
    if (json_object_has_member(o, "has_walk_detail")) {
        summary_out->has_walk_detail = json_object_get_boolean_member(o, "has_walk_detail");
    }
    if (json_object_has_member(o, "walk_steps")) {
        summary_out->walk_steps = (int)json_object_get_int_member(o, "walk_steps");
    }
    if (json_object_has_member(o, "walk_avg_pace_sec_per_km")) {
        summary_out->walk_avg_pace_sec_per_km = (int)json_object_get_int_member(o, "walk_avg_pace_sec_per_km");
    }
    if (json_object_has_member(o, "walk_avg_speed_kmh")) {
        summary_out->walk_avg_speed_kmh = (float)json_object_get_double_member(o, "walk_avg_speed_kmh");
    }
    if (json_object_has_member(o, "walk_step_length_cm")) {
        summary_out->walk_step_length_cm = (int)json_object_get_int_member(o, "walk_step_length_cm");
    }
    if (json_object_has_member(o, "walk_avg_cadence_spm")) {
        summary_out->walk_avg_cadence_spm = (int)json_object_get_int_member(o, "walk_avg_cadence_spm");
    }
    if (json_object_has_member(o, "walk_max_cadence_spm")) {
        summary_out->walk_max_cadence_spm = (int)json_object_get_int_member(o, "walk_max_cadence_spm");
    }
    if (json_object_has_member(o, "walk_te_aerobic")) {
        summary_out->walk_te_aerobic = (float)json_object_get_double_member(o, "walk_te_aerobic");
    }
    if (json_object_has_member(o, "walk_te_anaerobic")) {
        summary_out->walk_te_anaerobic = (float)json_object_get_double_member(o, "walk_te_anaerobic");
    }
    if (json_object_has_member(o, "walk_training_load")) {
        summary_out->walk_training_load = (int)json_object_get_int_member(o, "walk_training_load");
    }
    if (json_object_has_member(o, "walk_recovery_min")) {
        summary_out->walk_recovery_min = (int)json_object_get_int_member(o, "walk_recovery_min");
    }
    if (json_object_has_member(o, "walk_vitality_points")) {
        summary_out->walk_vitality_points = (int)json_object_get_int_member(o, "walk_vitality_points");
    }
    read_hr_zones(o, &summary_out->hr_zones);
    read_series(o, summary_out);
    g_object_unref(parser);
    return TRUE;
}
