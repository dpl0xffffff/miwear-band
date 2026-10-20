    public EcgMeasureParser() {
        int i = 1;
        FitnessDataBaseParser.OneDimenDataType oneDimenDataType = new FitnessDataBaseParser.OneDimenDataType(-3, 4, i, false, null, false, null, 120, null);
        FitnessDataBaseParser.OneDimenDataType oneDimenDataType2 = new FitnessDataBaseParser.OneDimenDataType(-2, i, 1, false, null, false, null, 120, null);
        int i2 = 1;
        FitnessDataBaseParser.OneDimenDataType oneDimenDataType3 = new FitnessDataBaseParser.OneDimenDataType(-1, 2, i2, false, null, false, null, 120, null);
        int i3 = 1;
        this.reportDataTypeArray = new FitnessDataBaseParser.OneDimenDataType[]{oneDimenDataType, oneDimenDataType2, oneDimenDataType3, new FitnessDataBaseParser.OneDimenDataType(0, i2, i3, false, null, false, null, 120, null), new FitnessDataBaseParser.OneDimenDataType(2, i3, 2, false, null, false, null, 120, null), new FitnessDataBaseParser.OneDimenDataType(3, 4, 2, false, null, false, null, 120, null), new FitnessDataBaseParser.OneDimenDataType(-4, 1, 2, false, null, false, null, 120, null)};
        this.recordDataTypeArray = new FitnessDataBaseParser.OneDimenDataType[]{new FitnessDataBaseParser.OneDimenDataType(1, 4, 1, true, null, false, null, 112, null)};
    }
