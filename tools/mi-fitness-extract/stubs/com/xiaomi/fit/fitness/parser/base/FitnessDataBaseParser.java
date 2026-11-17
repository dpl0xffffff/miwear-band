package com.xiaomi.fit.fitness.parser.base;
import kotlin.jvm.internal.DefaultConstructorMarker;
public abstract class FitnessDataBaseParser {
  public static final class DependDataType {
    public final int type; public final Integer[] values;
    public DependDataType(int t, Integer[] v) { type = t; values = v; }
  }
  public static final class OneDimenDataType {
    public final int type, byteCount, supportVersion; public final boolean isFloat, isUnsigned;
    public final DependDataType dependData; public final Integer countsType;
    public OneDimenDataType(int i, int i2, int i3, boolean z, DependDataType d, boolean z2, Integer num) {
      type = i; byteCount = i2; supportVersion = i3; isFloat = z; dependData = d; isUnsigned = z2; countsType = num;
    }
    public OneDimenDataType(int i, int i2, int i3, boolean z, DependDataType d, boolean z2, Integer num, int i4, DefaultConstructorMarker m) {
      this(i, i2, i3, (i4 & 8) != 0 ? false : z, (i4 & 16) != 0 ? null : d, (i4 & 32) != 0 ? true : z2, (i4 & 64) != 0 ? null : num);
    }
    public OneDimenDataType(int i, int i2, int i3, DependDataType d) { this(i, i2, i3, false, d, false, null, 96, null); }
  }
}
