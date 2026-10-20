    public AbnormalRecordParser() {
        FitnessDataBaseParser.OneDimenDataType oneDimenDataType = new FitnessDataBaseParser.OneDimenDataType(-1, 4, 1, false, null, false, null, 120, null);
        int i = 1;
        FitnessDataBaseParser.OneDimenDataType oneDimenDataType2 = new FitnessDataBaseParser.OneDimenDataType(-2, 4, i, false, null, false, null, 120, null);
        int i2 = 1;
        FitnessDataBaseParser.OneDimenDataType oneDimenDataType3 = new FitnessDataBaseParser.OneDimenDataType(-3, i, i2, false, null, false, null, 120, null);
        int i3 = 2;
        this.dataTypeArray = new FitnessDataBaseParser.OneDimenDataType[]{oneDimenDataType, oneDimenDataType2, oneDimenDataType3, new FitnessDataBaseParser.OneDimenDataType(-4, i2, i3, false, null, false, null, 120, null), new FitnessDataBaseParser.OneDimenDataType(-5, i3, 1, false, null, false, null, 120, null)};
    }
