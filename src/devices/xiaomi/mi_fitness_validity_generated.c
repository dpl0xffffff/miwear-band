/* Generated from FitnessDataValidity (Mi Fitness APK). Do not edit by hand; regen via scripts/gen_mi_fitness_validity.py */

static int getAbnormalRecordValidityLen(int version) {
    return (version == 1 || version == 2) ? 0 : -1;
}

static int getAllDaySleepValidityLen(int version) {
    switch (version) {
    case 1:
    case 2:
    case 3:
    case 4:
        return 1;
    case 5:
        return 2;
    case 6:
        return 3;
    default:
        return -1;
    }
}

static int getBasketballRecordValidityLen(int version) {
    return version == 1 ? 2 : -1;
}

static int getBasketballReportValidityLen(int version) {
    return (version == 1 || version == 2 || version == 3 || version == 4) ? 4
                                                                          : (version != 5 ? -1 : 5);
}

static int getBikingInRecordValidityLen(int version) {
    switch (version) {
    case 1:
        return 1;
    case 2:
    case 3:
    case 4:
        return 2;
    case 5:
        return 3;
    case 6:
        return 4;
    default:
        return -1;
    }
}

static int getBikingInReportValidityLen(int version) {
    switch (version) {
    case 1:
    case 2:
        return 2;
    case 3:
    case 4:
        return 3;
    case 5:
    case 6:
        return 5;
    case 7:
        return 6;
    case 8:
        return 7;
    case 9:
        return 8;
    case 10:
        return 9;
    default:
        return -1;
    }
}

static int getBikingOutRecordValidityLen(int version) {
    return (version == 1 || version == 2) ? 2 : -1;
}

static int getBikingOutReportValidityLen(int version) {
    return (version == 1 || version == 2 || version == 3) ? 4 : (version != 4 ? -1 : 5);
}

static int getDailyRecordValidityLen(int version) {
    if (version == 1 || version == 2)
        return 4;
    if (version != 3)
        return (version == 4 || version == 5) ? 6 : -1;
    return 5;
}

static int getDailyReportValidityLen(int version) {
    switch (version) {
    case 1:
    case 2:
        return 2;
    case 3:
    case 4:
        return 3;
    case 5:
        return 4;
    case 6:
    case 7:
        return 5;
    default:
        return -1;
    }
}

static int getDayLightSleepValidityLen(int version) {
    return (version == 1 || version == 2) ? 1 : -1;
}

static int getEcgMeasureValidityLen(int version) {
    return (version == 1 || version == 2) ? 1 : -1;
}

static int getEcgRecordValidityLen(int version) {
    return version == 1 ? 1 : -1;
}

static int getEcgReportValidityLen(int version) {
    return (version == 1 || version == 2) ? 1 : -1;
}

static int getEllipticalMachineRecordValidityLen(int version) {
    return (version == 1 || version == 2) ? 1 : (version != 3 ? -1 : 2);
}

static int getEllipticalMachineReportValidityLen(int version) {
    switch (version) {
    case 1:
    case 2:
        return 3;
    case 3:
    case 4:
    case 5:
    case 6:
        return 4;
    case 7:
        return 6;
    default:
        return -1;
    }
}

static int getFreeTrainingRecordValidityLen(int version) {
    switch (version) {
    case 1:
    case 2:
        return 1;
    case 3:
    case 4:
    case 5:
    case 6:
        return 2;
    default:
        return -1;
    }
}

static int getFreeTrainingReportValidityLen(int version) {
    switch (version) {
    case 1:
    case 2:
        return 2;
    case 3:
    case 4:
    case 5:
        return 3;
    case 6:
        return 4;
    case 7:
        return 5;
    case 8:
    case 9:
    case 10:
        return 6;
    case 11:
    case 12:
        return 7;
    case 13:
    case 14:
        return 8;
    default:
        return -1;
    }
}

static int getGolfRecordValidityLen(int version) {
    return version == 1 ? 1 : -1;
}

static int getGolfReportValidityLen(int version) {
    return version != 1 ? ((version == 2 || version == 3 || version == 4) ? 3 : -1) : 2;
}

static int getHiIntervalTrainingReportValidityLen(int version) {
    switch (version) {
    case 1:
    case 2:
        return 3;
    case 3:
    case 4:
    case 5:
    case 6:
        return 4;
    case 7:
        return 5;
    default:
        return -1;
    }
}

static int getHikingOutReportValidityLen(int version) {
    return (version == 1 || version == 2) ? 4 : (version != 3 ? -1 : 5);
}

static int getManualMeasureValidityLen(int version) {
    return version != 1 ? ((version == 2 || version == 3) ? 0 : -1) : 1;
}

static int getNightSleepValidityLen(int version) {
    return (version == 1 || version == 2) ? 2 : -1;
}

static int getOrdinaryBallRecordValidityLen(int version) {
    return version == 1 ? 2 : -1;
}

static int getOrdinaryBallReportValidityLen(int version) {
    if (version == 1)
        return 4;
    if (version == 2 || version == 3 || version == 4)
        return 5;
    return version != 5 ? -1 : 6;
}

static int getRopeSkippingRecordValidityLen(int version) {
    switch (version) {
    case 1:
    case 2:
    case 3:
    case 4:
        return 1;
    case 5:
    case 6:
        return 3;
    default:
        return -1;
    }
}

static int getRopeSkippingReportValidityLen(int version) {
    switch (version) {
    case 1:
    case 2:
        return 4;
    case 3:
    case 4:
    case 5:
    case 6:
        return 5;
    case 7:
        return 6;
    default:
        return -1;
    }
}

static int getRowingMachineRecordValidityLen(int version) {
    return (version == 1 || version == 2 || version == 3 || version == 4) ? 2 : -1;
}

static int getRowingMachineReportValidityLen(int version) {
    switch (version) {
    case 1:
        return 3;
    case 2:
    case 3:
    case 4:
        return 4;
    case 5:
    case 6:
    case 7:
        return 5;
    case 8:
        return 6;
    default:
        return -1;
    }
}

static int getRunningInRecordValidityLen(int version) {
    switch (version) {
    case 1:
    case 2:
    case 3:
    case 4:
        return 2;
    case 5:
        return 3;
    case 6:
        return 4;
    case 7:
    case 8:
        return 5;
    case 9:
    case 10:
        return 6;
    default:
        return -1;
    }
}

static int getRunningInReportValidityLen(int version) {
    switch (version) {
    case 1:
    case 2:
    case 3:
        return 3;
    case 4:
    case 5:
        return 4;
    case 6:
        return 5;
    case 7:
    case 8:
    case 9:
        return 6;
    case 10:
        return 8;
    case 11:
        return 9;
    case 12:
        return 11;
    case 13:
        return 12;
    case 14:
        return 13;
    default:
        return -1;
    }
}

static int getSkiRecordValidityLen(int version) {
    return version != 1 ? ((version == 2 || version == 3 || version == 4) ? 3 : -1) : 2;
}

static int getSkiReportValidityLen(int version) {
    switch (version) {
    case 1:
        return 5;
    case 2:
    case 3:
    case 4:
        return 6;
    case 5:
    case 6:
        return 7;
    default:
        return -1;
    }
}

static int getSportDivingRecordValidityLen(int version) {
    return (version == 1 || version == 2) ? 2 : -1;
}

static int getSportDivingReportValidityLen(int version) {
    return (version == 1 || version == 2) ? 4 : -1;
}

static int getSportGameRecordValidityLen(int version) {
    return version == 1 ? 2 : -1;
}

static int getSportGameReportValidityLen(int version) {
    return version == 1 ? 6 : -1;
}

static int getSportGpsValidityLen(int version) {
    if (version == 1 || version == 2 || version == 3 || version == 4)
        return 1;
    return version != 5 ? -1 : 2;
}

static int getSportNoStepRecordValidityLen(int version) {
    switch (version) {
    case 1:
    case 2:
        return 2;
    case 3:
    case 4:
    case 5:
    case 6:
        return 3;
    case 7:
        return 4;
    default:
        return -1;
    }
}

static int getSportNoStepReportValidityLen(int version) {
    switch (version) {
    case 1:
    case 2:
        return 5;
    case 3:
    case 4:
    case 5:
        return 6;
    case 6:
        return 7;
    case 7:
    case 8:
        return 8;
    case 9:
        return 9;
    default:
        return -1;
    }
}

static int getSportOutdoorRecordValidityLen(int version) {
    return (version == 1 || version == 2) ? 2 : -1;
}

static int getSportOutdoorReportValidityLen(int version) {
    if (version == 1 || version == 2 || version == 3)
        return 4;
    return version != 4 ? -1 : 5;
}

static int getSportRockClimbingRecordValidityLen(int version) {
    return (version == 1 || version == 2) ? 2 : -1;
}

static int getSportRockClimbingReportValidityLen(int version) {
    return (version == 1 || version == 2) ? 7 : -1;
}

static int getSportStepRecordValidityLen(int version) {
    switch (version) {
    case 1:
        return 2;
    case 2:
    case 3:
    case 4:
        return 3;
    case 5:
        return 5;
    case 6:
    case 7:
        return 6;
    case 8:
    case 9:
        return 7;
    default:
        return -1;
    }
}

static int getSportStepReportValidityLen(int version) {
    switch (version) {
    case 1:
        return 5;
    case 2:
    case 3:
        return 6;
    case 4:
        return 7;
    case 5:
    case 6:
        return 9;
    case 7:
    case 8:
        return 12;
    case 9:
        return 13;
    default:
        return -1;
    }
}

static int getSwimmingRecordValidityLen(int version) {
    return (version == 1 || version == 2) ? 2 : (version != 3 ? -1 : 3);
}

static int getSwimmingReportValidityLen(int version) {
    switch (version) {
    case 1:
        return 2;
    case 2:
    case 3:
    case 4:
        return 3;
    case 5:
    case 6:
        return 4;
    case 7:
        return 5;
    case 8:
        return 8;
    default:
        return -1;
    }
}

static int getTemperatureMeasureValidityLen(int version) {
    return version == 1 ? 0 : -1;
}

static int getTriathlonRecordValidityLen(int version) {
    return (version == 1 || version == 2) ? 0 : -1;
}

static int getTriathlonReportValidityLen(int version) {
    return (version == 1 || version == 2 || version == 3 || version == 4) ? 2 : -1;
}

static int getUserProfileValidityLen(int version) {
    return (version == 1 || version == 2 || version == 3 || version == 4) ? 2 : -1;
}

static int getWeightRecordValidityLen(int version) {
    return version == 1 ? 2 : -1;
}

static int getYogaReportValidityLen(int version) {
    switch (version) {
    case 1:
        return 2;
    case 2:
    case 3:
        return 3;
    case 4:
    case 5:
    case 6:
        return 4;
    case 7:
        return 5;
    default:
        return -1;
    }
}
