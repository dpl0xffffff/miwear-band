package com.xiaomi.fit.data.common.data.mi;
public final class FitnessDataId {
  final int dataType, sportType, dailyType, fileType, version;
  public FitnessDataId(int dataType, int sportType, int dailyType, int fileType, int version) {
    this.dataType = dataType; this.sportType = sportType; this.dailyType = dailyType; this.fileType = fileType; this.version = version;
  }
  public int getDataType() { return dataType; } public int getSportType() { return sportType; }
  public int getDailyType() { return dailyType; } public int getFileType() { return fileType; } public int getVersion() { return version; }
}
