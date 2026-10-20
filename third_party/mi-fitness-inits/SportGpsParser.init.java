    public SportGpsParser() {
        FitnessDataBaseParser.OneDimenDataType oneDimenDataType = new FitnessDataBaseParser.OneDimenDataType(0, 4, 1, false, null, false, null, 120, null);
        FitnessDataBaseParser.OneDimenDataType oneDimenDataType2 = new FitnessDataBaseParser.OneDimenDataType(1, 4, 1, true, null, false, null, 112, null);
        char c = 1 == true ? 1 : 0;
        FitnessDataBaseParser.OneDimenDataType oneDimenDataType3 = new FitnessDataBaseParser.OneDimenDataType(2, 4, c, true, null, false, null, 112, null);
        int i = 4;
        int i2 = 2;
        this.dataTypeArray = new FitnessDataBaseParser.OneDimenDataType[]{oneDimenDataType, oneDimenDataType2, oneDimenDataType3, new FitnessDataBaseParser.OneDimenDataType(3, i, i2, true, null, false, null, 112, null), new FitnessDataBaseParser.OneDimenDataType(i, i2, 2, false, null, false, null, 120, null), new FitnessDataBaseParser.OneDimenDataType(5, 0, 2, false, null, false, null, 120, null), new FitnessDataBaseParser.OneDimenDataType(6, 4, 3, true, null, false, null, 112, null), new FitnessDataBaseParser.OneDimenDataType(7, 4, 3, true, null, false, null, 112, null)};
    }
