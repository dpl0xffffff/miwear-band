#include "salus/activity/activity.h"
#include "salus/app.h"
#include "salus/sleep/mi_sleep_duration.h"

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <string.h>

static gboolean parse_algo_json(const char *json_text,
                                GArray *stage_rows,
                                int *deep_min,
                                int *light_min,
                                int *rem_min,
                                int *awake_min,
                                int *total_min) {
    if (json_text == NULL || stage_rows == NULL) {
        return FALSE;
    }
    JsonParser *parser = json_parser_new();
    GError *err = NULL;
    if (!json_parser_load_from_data(parser, json_text, -1, &err)) {
        g_clear_error(&err);
        g_object_unref(parser);
        return FALSE;
    }
    JsonNode *root = json_parser_get_root(parser);
    if (root == NULL || json_node_get_node_type(root) != JSON_NODE_OBJECT) {
        g_object_unref(parser);
        return FALSE;
    }
    JsonObject *obj = json_node_get_object(root);
    if (deep_min != NULL && json_object_has_member(obj, "deepDuration")) {
        *deep_min = (int)json_object_get_int_member(obj, "deepDuration");
    }
    if (light_min != NULL && json_object_has_member(obj, "lightDuration")) {
        *light_min = (int)json_object_get_int_member(obj, "lightDuration");
    }
    if (rem_min != NULL && json_object_has_member(obj, "remDuration")) {
        *rem_min = (int)json_object_get_int_member(obj, "remDuration");
    }
    if (awake_min != NULL && json_object_has_member(obj, "wakeDuration")) {
        *awake_min = (int)json_object_get_int_member(obj, "wakeDuration");
    }
    if (total_min != NULL && json_object_has_member(obj, "sleepDuration")) {
        *total_min = (int)json_object_get_int_member(obj, "sleepDuration");
    }
    if (deep_min != NULL || light_min != NULL || rem_min != NULL || awake_min != NULL || total_min != NULL) {
        SalusSleepStageDurations nd = {
            .deep_min = deep_min != NULL ? *deep_min : -1,
            .light_min = light_min != NULL ? *light_min : -1,
            .rem_min = rem_min != NULL ? *rem_min : -1,
            .awake_min = awake_min != NULL ? *awake_min : -1,
            .total_min = total_min != NULL ? *total_min : -1,
        };
        salus_sleep_normalize_stage_durations(&nd);
        if (deep_min != NULL) {
            *deep_min = nd.deep_min;
        }
        if (light_min != NULL) {
            *light_min = nd.light_min;
        }
        if (rem_min != NULL) {
            *rem_min = nd.rem_min;
        }
        if (awake_min != NULL) {
            *awake_min = nd.awake_min;
        }
        if (total_min != NULL) {
            *total_min = nd.total_min;
        }
    }
    JsonArray *stages = NULL;
    if (json_object_has_member(obj, "stages")) {
        stages = json_object_get_array_member(obj, "stages");
    }
    if (stages == NULL) {
        g_object_unref(parser);
        return stage_rows->len > 0;
    }
    const guint n = json_array_get_length(stages);
    for (guint i = 0; i < n; i++) {
        JsonObject *st = json_array_get_object_element(stages, i);
        if (st == NULL) {
            continue;
        }
        const int64_t start_ms = json_object_has_member(st, "startTime")
                                     ? json_object_get_int_member(st, "startTime")
                                     : (json_object_has_member(st, "start") ? json_object_get_int_member(st, "start")
                                                                            : 0);
        const int state = json_object_has_member(st, "state") ? (int)json_object_get_int_member(st, "state") : 0;
        if (start_ms <= 0) {
            continue;
        }
        SalusSleepStageRow row = {start_ms, salus_normalize_sleep_stage(state)};
        g_array_append_val(stage_rows, row);
    }
    g_object_unref(parser);
    return stage_rows->len > 0;
}

gboolean salus_sleep_run_external_algo_cmd(AppState *state,
                                            const uint8_t *sleep_src,
                                            size_t sleep_src_len,
                                            GArray *stage_rows,
                                            int *deep_min,
                                            int *light_min,
                                            int *rem_min,
                                            int *awake_min,
                                            int *total_min,
                                            uint32_t bed_sec,
                                            uint32_t wake_sec) {
    const char *cmd_template = g_getenv("SALUS_MI_SLEEP_ALGO_CMD");
    if (cmd_template == NULL || cmd_template[0] == '\0') {
        return FALSE;
    }

    gchar *path = g_build_filename(g_get_user_runtime_dir(), "salus-sleep-src.bin", NULL);
    if (!g_file_set_contents(path, (const char *)sleep_src, (gssize)sleep_src_len, NULL)) {
        g_free(path);
        return FALSE;
    }

    /* Pass bed/wake to the helper via its own environment: g_setenv() on our process is not
     * thread-safe (GDBus/GIO worker threads read the environment) and leaked stale values into
     * later invocations. */
    gchar **envp = g_get_environ();
    if (bed_sec > 0) {
        gchar bed_env[32];
        (void)g_snprintf(bed_env, sizeof(bed_env), "%u", bed_sec);
        envp = g_environ_setenv(envp, "SALUS_SLEEP_BED_SEC", bed_env, TRUE);
    }
    if (wake_sec > 0) {
        gchar wake_env[32];
        (void)g_snprintf(wake_env, sizeof(wake_env), "%u", wake_sec);
        envp = g_environ_setenv(envp, "SALUS_SLEEP_WAKE_SEC", wake_env, TRUE);
    }

    gchar *cmd = g_strdup_printf(cmd_template, path);
    GError *err = NULL;
    gchar *stdout_buf = NULL;
    gchar *stderr_buf = NULL;
    gint exit_status = 0;
    gchar **argv = NULL;
    /* Same argv parsing and PATH lookup as g_spawn_command_line_sync(), plus envp. */
    const gboolean ok = g_shell_parse_argv(cmd, NULL, &argv, &err) &&
                        g_spawn_sync(NULL, argv, envp, G_SPAWN_SEARCH_PATH, NULL, NULL,
                                     &stdout_buf, &stderr_buf, &exit_status, &err);
    g_strfreev(argv);
    g_strfreev(envp);
    g_free(cmd);
    g_remove(path);
    g_free(path);

    if (!ok || exit_status != 0) {
        if (state != NULL && stderr_buf != NULL && stderr_buf[0] != '\0') {
            gchar *line = g_strdup_printf("Mi Fitness sleep algo helper failed: %s", stderr_buf);
            append_log(state, line);
            g_free(line);
        }
        g_clear_error(&err);
        g_free(stdout_buf);
        g_free(stderr_buf);
        return FALSE;
    }

    const gboolean parsed =
        parse_algo_json(stdout_buf, stage_rows, deep_min, light_min, rem_min, awake_min, total_min);
    g_free(stdout_buf);
    g_free(stderr_buf);
    return parsed;
}
