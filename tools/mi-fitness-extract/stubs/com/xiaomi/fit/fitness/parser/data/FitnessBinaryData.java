package com.xiaomi.fit.fitness.parser.data;
import com.xiaomi.fit.data.common.data.mi.FitnessDataId;
public final class FitnessBinaryData {
  private final FitnessDataId id;
  public FitnessBinaryData(FitnessDataId id) { this.id = id; }
  public FitnessDataId getDataId() { return id; }
}
