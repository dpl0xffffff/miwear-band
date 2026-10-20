#include "miwear/activity/mi_fitness_dispatch.h"
#include "miwear/activity/mi_fitness_data.h"
#include "miwear/activity/mi_fitness_health.h"
#include "miwear/activity/mi_fitness_sleep_stages.h"
#include "miwear/activity/schema_reader.h"
#include "miwear/app.h"

const char *mi_fitness_daily_parser_name(int daily_type, int file_type) {
    daily_type = mi_fitness_apk_daily_type(daily_type);
    if (daily_type == 0) {
        return file_type == 0 ? "DailyRecordParser" : "DailyReportParser";
    }
    switch (daily_type) {
        case 2:
            return "DaytimeSleepParser";
        case 3:
            return "NightSleepParser";
        case 5:
            return "UserProfileParser";
        case 6:
            return "ManualMeasureParser";
        case 8:
            return "AllDaySleepParser";
        case 9:
            return "AbnormalRecordParser";
        case 10:
            return "WeightRecordParser";
        case 11:
            return "EcgMeasureParser";
        case 12:
            return "TemperatureMeasureParser";
        default:
            return "DailyReportParser";
    }
}

const char *mi_fitness_sport_report_parser_name(int sport_type) {
    static const int outdoor[] = {1, 2, 4, 5};
    if (sport_type == 22) {
        return "OutdoorStepReportParser";
    }
    for (size_t i = 0; i < sizeof(outdoor) / sizeof(outdoor[0]); i++) {
        if (sport_type == outdoor[i]) {
            return "OutdoorSportReportParser";
        }
    }
    static const struct {
        int sport;
        const char *name;
    } map[] = {
        {3, "IndoorRunReportParser"},
        {6, "OutdoorBikingReportParser"},
        {7, "IndoorBikingReportParser"},
        {8, "FreeTrainingReportParser"},
        {9, "SwimmingReportParser"},
        {10, "SwimmingReportParser"},
        {11, "EllipticalMachineReportParser"},
        {12, "YogaReportParser"},
        {13, "RowingMachineReportParser"},
        {14, "RopeSkippingReportParser"},
        {15, "HikingReportParser"},
        {16, "HITrainingReportParser"},
        {17, "TriathlonReportParser"},
        {18, "OrdinaryBallReportParser"},
        {19, "BasketballReportParser"},
        {20, "GolfReportParser"},
        {21, "SkiReportParser"},
        {23, "OutdoorNoStepReportParser"},
        {24, "RockClimbingReportParser"},
        {25, "DivingReportParser"},
        {26, "GameModeReportRarser"},
    };
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if (map[i].sport == sport_type) {
            return map[i].name;
        }
    }
    return "SportReportSchemaBaseParser";
}

const char *mi_fitness_sport_record_parser_name(int sport_type) {
    static const int outdoor[] = {1, 2, 4, 5, 15};
    for (size_t i = 0; i < sizeof(outdoor) / sizeof(outdoor[0]); i++) {
        if (sport_type == outdoor[i]) {
            return "OutdoorSportRecordParser";
        }
    }
    if (sport_type == 22) {
        return "OutdoorStepRecordParser";
    }
    return "SportRecordSchemaBaseParser";
}

void mi_fitness_apply_health_payload(AppState *state,
                                       uint32_t ts,
                                       int8_t tz,
                                       int daily_type,
                                       int detail,
                                       int version,
                                       const uint8_t *payload,
                                       size_t payload_len) {
    if (state == NULL || payload == NULL) {
        return;
    }

    const int apk_daily = mi_fitness_apk_daily_type(daily_type);
    const char *parser = mi_fitness_daily_parser_name(daily_type, detail);
    gboolean schema_ok = FALSE;
    gchar *rel = miwear_schema_resolve_daily(daily_type, detail, version);
    if (rel != NULL) {
        gchar *schema_path = g_build_filename(miwear_schema_dir(), rel, NULL);
        JsonNode *root = NULL;
        gboolean applied = FALSE;
        if (miwear_schema_load_root(schema_path, &root)) {
            GBytes *wire = miwear_schema_sport_wire_buffer(payload, payload_len);
            if (wire != NULL) {
                const gsize wire_len = g_bytes_get_size(wire);
                JsonNode *decoded = NULL;
                if (miwear_schema_decode(root,
                                         g_bytes_get_data(wire, NULL),
                                         wire_len,
                                         &decoded)) {
                    gchar *line = g_strdup_printf(
                        "Mi Fitness daily wire=%d apk=%d detail=%d v%d via %s (schema %s, decoded ok)",
                        daily_type,
                        apk_daily,
                        detail,
                        version,
                        parser,
                        rel);
                    append_log(state, line);
                    g_free(line);
                    mi_fitness_apply_schema_health(state, ts, apk_daily, detail, decoded, payload, payload_len);
                    applied = TRUE;
                    schema_ok = TRUE;
                    json_node_unref(decoded);
                }
            }
            if (wire != NULL) {
                g_bytes_unref(wire);
            }
            json_node_unref(root);
        }
        g_free(schema_path);
        g_free(rel);
        (void)applied;
    }

    if (!schema_ok) {
        if (mi_fitness_apply_native_health(state, ts, daily_type, detail, version, payload, payload_len)) {
            return;
        }

        gchar *line = g_strdup_printf(
            "Mi Fitness daily wire=%d apk=%d detail=%d v%d — %s native layout (%zu bytes, ts=%u tz=%d)",
            daily_type,
            apk_daily,
            detail,
            version,
            parser,
            payload_len,
            ts,
            tz);
        append_log(state, line);
        g_free(line);
    }

    if (apk_daily == 8 && version >= 5) {
        const uint8_t *feature = NULL;
        size_t feature_len = 0;
        uint32_t bed = 0;
        uint32_t wake = 0;
        if (mi_fitness_full_day_sleep_feature_tail(payload, payload_len, version, &feature, &feature_len) &&
            mi_fitness_full_day_sleep_bed_wake(payload, payload_len, version, &bed, &wake)) {
            mi_fitness_apply_sleep_stages_from_feature(state,
                                                       payload,
                                                       payload_len,
                                                       version,
                                                       feature,
                                                       feature_len,
                                                       bed,
                                                       wake);
        }
    }
}
