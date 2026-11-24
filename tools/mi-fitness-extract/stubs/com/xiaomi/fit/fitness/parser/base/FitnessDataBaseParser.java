package com.xiaomi.fit.fitness.parser.base;
import kotlin.jvm.internal.DefaultConstructorMarker;
public abstract class FitnessDataBaseParser {
  public static final class DependDataType {
    public final int type; public final Integer[] values;
    public DependDataType(int t, Integer[] v) { type = t; values = v; }
  }
  public static final class DynamicValidDataType {
    public final int dataType, dependType, existValue;
    public DynamicValidDataType(int a, int b, int c) { dataType = a; dependType = b; existValue = c; }
  }
  public static final class DataItemLen {
    public final int byteSize, startBitIndex, bitSize;
    public DataItemLen(int a, int b, int c) { byteSize = a; startBitIndex = b; bitSize = c; }
    public DataItemLen(int b, int c) { this(0, b, c); }
    public DataItemLen(int a) { this(a, 0, 0); }
  }
  public static final class FourDimenType {
    public final int high; public final Integer middle, low;
    public FourDimenType(int h, Integer m, Integer l) { high = h; middle = m; low = l; }
    public FourDimenType(int h, Integer m, Integer l, int mask, DefaultConstructorMarker x) {
      this(h, (mask & 2) != 0 ? null : m, (mask & 4) != 0 ? null : l);
    }
  }
  public static final class FourDimenDataInfo {
    public final FourDimenType dataType; public final int byteSize, supportVersion;
    public final DataItemLen high, middle, low; public final DynamicValidDataType dynamicDataType;
    public final boolean isUnsigned; public final Integer maxSupportVersion;
    public FourDimenDataInfo(FourDimenType t, int bs, DataItemLen h, DataItemLen m, DataItemLen l, int sv,
                             DynamicValidDataType d, boolean u, Integer max) {
      dataType = t; byteSize = bs; high = h; middle = m; low = l; supportVersion = sv; dynamicDataType = d; isUnsigned = u; maxSupportVersion = max;
    }
    public FourDimenDataInfo(FourDimenType t, int bs, DataItemLen h, DataItemLen m, DataItemLen l, int sv,
                             DynamicValidDataType d, boolean u, Integer max, int mask, DefaultConstructorMarker x) {
      this(t, bs, h, m, l, sv, (mask & 64) != 0 ? null : d, (mask & 128) != 0 ? true : u, (mask & 256) != 0 ? null : max);
    }
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
