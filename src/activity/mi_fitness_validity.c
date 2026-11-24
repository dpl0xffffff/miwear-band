#include "salus/activity/mi_fitness_validity.h"
#include "salus/activity/mi_fitness_data.h"

/* Leaf version→validity-byte-length helpers (APK FitnessDataValidity). */
#include "mi_fitness_validity_generated.c"

static int sport_report_validity_len(const SalusFitnessDataId *id) {
    const int version = id->version;
    switch (id->sport_type) {
        case 1:
        case 2:
        case 4:
        case 5:
            return getSportOutdoorReportValidityLen(version);
        case 3:
            return getRunningInReportValidityLen(version);
        case 6:
            return getBikingOutReportValidityLen(version);
        case 7:
            return getBikingInReportValidityLen(version);
        case 8:
            return getFreeTrainingReportValidityLen(version);
        case 9:
        case 10:
            return getSwimmingReportValidityLen(version);
        case 11:
            return getEllipticalMachineReportValidityLen(version);
        case 12:
            return getYogaReportValidityLen(version);
        case 13:
            return getRowingMachineReportValidityLen(version);
        case 14:
            return getRopeSkippingReportValidityLen(version);
        case 15:
            return getHikingOutReportValidityLen(version);
        case 16:
            return getHiIntervalTrainingReportValidityLen(version);
        case 17:
            return getTriathlonReportValidityLen(version);
        case 18:
            return getOrdinaryBallReportValidityLen(version);
        case 19:
            return getBasketballReportValidityLen(version);
        case 20:
            return getGolfReportValidityLen(version);
        case 21:
            return getSkiReportValidityLen(version);
        case 22:
            return getSportStepReportValidityLen(version);
        case 23:
            return getSportNoStepReportValidityLen(version);
        case 24:
            return getSportRockClimbingReportValidityLen(version);
        case 25:
            return getSportDivingReportValidityLen(version);
        case 26:
            return getSportGameReportValidityLen(version);
        default:
            return -1;
    }
}

static int sport_record_validity_len(const SalusFitnessDataId *id) {
    const int version = id->version;
    switch (id->sport_type) {
        case 1:
        case 2:
        case 4:
        case 5:
        case 15:
            return getSportOutdoorRecordValidityLen(version);
        case 3:
            return getRunningInRecordValidityLen(version);
        case 6:
            return getBikingOutRecordValidityLen(version);
        case 7:
            return getBikingInRecordValidityLen(version);
        case 8:
        case 12:
        case 16:
            return getFreeTrainingRecordValidityLen(version);
        case 9:
        case 10:
            return getSwimmingRecordValidityLen(version);
        case 11:
            return getEllipticalMachineRecordValidityLen(version);
        case 13:
            return getRowingMachineRecordValidityLen(version);
        case 14:
            return getRopeSkippingRecordValidityLen(version);
        case 17:
            return getTriathlonRecordValidityLen(version);
        case 18:
            return getOrdinaryBallRecordValidityLen(version);
        case 19:
            return getBasketballRecordValidityLen(version);
        case 20:
            return getGolfRecordValidityLen(version);
        case 21:
            return getSkiRecordValidityLen(version);
        case 22:
            return getSportStepRecordValidityLen(version);
        case 23:
            return getSportNoStepRecordValidityLen(version);
        case 24:
            return getSportRockClimbingRecordValidityLen(version);
        case 25:
            return getSportDivingRecordValidityLen(version);
        case 26:
            return getSportGameRecordValidityLen(version);
        default:
            return -1;
    }
}

static int sport_validity_len(const SalusFitnessDataId *id) {
    switch (id->file_type) {
        case 0:
            return sport_record_validity_len(id);
        case 1:
            return sport_report_validity_len(id);
        case 3:
            return 0;
        default:
            return getSportGpsValidityLen(id->version);
    }
}

static int daily_validity_len(const SalusFitnessDataId *id) {
    const int version = id->version;
    const int daily = mi_fitness_apk_daily_type(id->daily_type);
    if (daily == 0) {
        return id->file_type == 1 ? getDailyReportValidityLen(version) : getDailyRecordValidityLen(version);
    }
    if (daily == 31) {
        return id->file_type == 1 ? getEcgReportValidityLen(version) : getEcgRecordValidityLen(version);
    }
    if (daily == 2) {
        return getDayLightSleepValidityLen(version);
    }
    if (daily == 3) {
        return getNightSleepValidityLen(version);
    }
    switch (daily) {
        case 5:
            return getUserProfileValidityLen(version);
        case 6:
            return getManualMeasureValidityLen(version);
        case 7:
        case 8:
            return getAllDaySleepValidityLen(version);
        case 9:
            return getAbnormalRecordValidityLen(version);
        case 10:
            return getWeightRecordValidityLen(version);
        case 11:
            return getEcgMeasureValidityLen(version);
        case 12:
            return getTemperatureMeasureValidityLen(version);
        default:
            return -1;
    }
}

int mi_fitness_data_validity_len(const SalusFitnessDataId *id) {
    if (id == NULL) {
        return -1;
    }
    return id->data_type == 1 ? sport_validity_len(id) : daily_validity_len(id);
}
