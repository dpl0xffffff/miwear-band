    public OutdoorSportRecordParser() {
        Integer num = null;
        FitnessDataBaseParser.FourDimenType[] fourDimenTypeArr = {new FitnessDataBaseParser.FourDimenType(2, 4, null, 4, null), new FitnessDataBaseParser.FourDimenType(5, null, num, 6, null), new FitnessDataBaseParser.FourDimenType(6, 7, 8), new FitnessDataBaseParser.FourDimenType(9, num, null, 6, null)};
        this.fourDimenTypeArray = fourDimenTypeArr;
        FitnessDataBaseParser.DataItemLen dataItemLen = null;
        this.dataInfoArray = new FitnessDataBaseParser.FourDimenDataInfo[]{new FitnessDataBaseParser.FourDimenDataInfo(fourDimenTypeArr[0], 1, new FitnessDataBaseParser.DataItemLen(4, 4), new FitnessDataBaseParser.DataItemLen(0, 4), dataItemLen, 1, null, false, null, 448, null), new FitnessDataBaseParser.FourDimenDataInfo(fourDimenTypeArr[1], 1, new FitnessDataBaseParser.DataItemLen(1), dataItemLen, null, 1, null, false, null, 448, null), new FitnessDataBaseParser.FourDimenDataInfo(fourDimenTypeArr[2], 1, new FitnessDataBaseParser.DataItemLen(7, 1), new FitnessDataBaseParser.DataItemLen(6, 1), new FitnessDataBaseParser.DataItemLen(0, 6), 1, null, false, null, 448, null), new FitnessDataBaseParser.FourDimenDataInfo(fourDimenTypeArr[3], 1, new FitnessDataBaseParser.DataItemLen(1), null, null, 1, null, false, null, 448, null)};
        this.initDataTypeArray = new FitnessDataBaseParser.OneDimenDataType[]{new FitnessDataBaseParser.OneDimenDataType(0, 4, 1, true, null, false, null, 112, null)};
    }
