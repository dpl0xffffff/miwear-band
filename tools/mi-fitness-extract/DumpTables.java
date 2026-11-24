import com.xiaomi.fit.fitness.parser.base.FitnessDataBaseParser.*;
import com.xiaomi.fit.fitness.parser.sport.base.SportReportBaseParser;
import com.xiaomi.fit.data.common.data.mi.*;
import java.util.*;
public class DumpTables {
  static final Object[][] RECORDS = {
    {new int[]{1,2,4,5,15}, "OutdoorSportRecordParser"}, {new int[]{3}, "IndoorRunRecordParser"},
    {new int[]{6}, "OutdoorBikingRecordParser"}, {new int[]{7}, "IndoorBikingRecordParser"},
    {new int[]{8}, "FreeTrainingRecordParser"}, {new int[]{9,10}, "SwimmingRecordParser"},
    {new int[]{11}, "EllipticalMachineRecordParser"}, {new int[]{12}, "YogaRecordParser"},
    {new int[]{13}, "RowingMachineRecordParser"}, {new int[]{14}, "RopeSkippingRecordParser"},
    {new int[]{16}, "HITrainingRecordParser"}, {new int[]{18}, "OrdinaryBallRecordParser"},
    {new int[]{19}, "BasketballRecordParser"}, {new int[]{20}, "GolfRecordParser"},
    {new int[]{21}, "SkiRecordParser"}, {new int[]{22}, "OutdoorStepRecordParser"},
    {new int[]{23}, "OutdoorNoStepRecordParser"}, {new int[]{24}, "RockClimbingRecordParser"},
    {new int[]{25}, "DivingRecordParser"}, {new int[]{26}, "GameModeRecordRarser"},
  };

  static String oneDimen(OneDimenDataType[] f) {
    if (f == null) return "null";
    StringBuilder sb = new StringBuilder("[");
    for (int i = 0; i < f.length; i++) {
      OneDimenDataType t = f[i];
      sb.append(i == 0 ? "" : ", ").append("[").append(t.type).append(",").append(t.byteCount).append(",").append(t.supportVersion).append(",")
        .append(t.isFloat ? 1 : 0).append(",").append(t.isUnsigned ? 1 : 0).append(",").append(t.countsType == null ? -1 : t.countsType).append(",");
      if (t.dependData == null) sb.append("null"); else sb.append("[").append(t.dependData.type).append(",").append(Arrays.toString(t.dependData.values)).append("]");
      sb.append("]");
    }
    return sb.append("]").toString();
  }

  static String len(DataItemLen l) {
    return l == null ? "null" : "[" + l.byteSize + "," + l.startBitIndex + "," + l.bitSize + "]";
  }

  static String fourDimen(FourDimenDataInfo[] f) {
    StringBuilder sb = new StringBuilder("[");
    for (int i = 0; i < f.length; i++) {
      FourDimenDataInfo d = f[i];
      sb.append(i == 0 ? "" : ", ").append("[");
      if (d.dataType == null) sb.append("null");
      else sb.append("[").append(d.dataType.high).append(",").append(d.dataType.middle).append(",").append(d.dataType.low).append("]");
      sb.append(",").append(d.byteSize).append(",").append(len(d.high)).append(",").append(len(d.middle)).append(",").append(len(d.low))
        .append(",").append(d.supportVersion).append(",");
      if (d.dynamicDataType == null) sb.append("null");
      else sb.append("[").append(d.dynamicDataType.dataType).append(",").append(d.dynamicDataType.dependType).append(",").append(d.dynamicDataType.existValue).append("]");
      sb.append(",").append(d.isUnsigned ? 1 : 0).append(",").append(d.maxSupportVersion == null ? -1 : d.maxSupportVersion).append("]");
    }
    return sb.append("]").toString();
  }

  static String records() throws Exception {
    StringBuilder sb = new StringBuilder(" \"records\": [\n");
    for (int r = 0; r < RECORDS.length; r++) {
      int[] sports = (int[]) RECORDS[r][0];
      String cls = (String) RECORDS[r][1];
      com.xiaomi.fit.fitness.parser.sport.base.SportRecordBaseParser p = (com.xiaomi.fit.fitness.parser.sport.base.SportRecordBaseParser)
          Class.forName("com.xiaomi.fit.fitness.parser.sport.record." + cls).getDeclaredConstructor().newInstance();
      sb.append("  {\"parser\": \"").append(cls).append("\", \"sports\": ").append(Arrays.toString(sports))
        .append(", \"group\": ").append(p instanceof com.xiaomi.fit.fitness.parser.sport.base.GroupSportRecordParser)
        .append(", \"swimming\": ").append(p.isSwimmingSport()).append(", \"distance_times\": ").append(p.getDistanceRecordTimes())
        .append(",\n   \"init\": ").append(oneDimen(p.getInitDataTypeArray()))
        .append(",\n   \"it_summary\": ").append(oneDimen(p.getITSummaryDataType()))
        .append(",\n   \"versions\": {");
      Map<Object, String> names = new IdentityHashMap<>();
      StringBuilder arrays = new StringBuilder();
      boolean firstV = true;
      for (int v = 1; v <= 30; v++) {
        if (FitnessDataValidity.INSTANCE.getDataValidityLen(new FitnessDataId(1, sports[0], 0, 0, v)) < 0) continue;
        com.xiaomi.fit.fitness.parser.sport.base.SportRecordBaseParser.chosenKind = null;
        com.xiaomi.fit.fitness.parser.sport.base.SportRecordBaseParser.chosenArray = null;
        p.parseRecord(new com.xiaomi.fit.fitness.parser.data.FitnessBinaryData(new FitnessDataId(1, sports[0], 0, 0, v)));
        Object arr = com.xiaomi.fit.fitness.parser.sport.base.SportRecordBaseParser.chosenArray;
        String kind = com.xiaomi.fit.fitness.parser.sport.base.SportRecordBaseParser.chosenKind;
        if (arr == null) continue;
        String name = names.get(arr);
        if (name == null) {
          name = kind + names.size();
          names.put(arr, name);
          arrays.append(arrays.length() == 0 ? "" : ",\n    ").append("\"").append(name).append("\": {\"kind\": \"").append(kind).append("\", \"fields\": ")
            .append(kind.equals("four") ? fourDimen((FourDimenDataInfo[]) arr) : oneDimen((OneDimenDataType[]) arr)).append("}");
        }
        sb.append(firstV ? "" : ", ").append("\"").append(v).append("\": \"").append(name).append("\"");
        firstV = false;
      }
      sb.append("},\n   \"arrays\": {\n    ").append(arrays).append("}}").append(r + 1 < RECORDS.length ? ",\n" : "\n");
    }
    return sb.append(" ],\n").toString();
  }

  static final Object[][] SPORTS = {
    {new int[]{1,2,4,5}, "OutdoorSportReportParser"}, {new int[]{3}, "IndoorRunReportParser"},
    {new int[]{6}, "OutdoorBikingReportParser"}, {new int[]{7}, "IndoorBikingReportParser"},
    {new int[]{8}, "FreeTrainingReportParser"}, {new int[]{9,10}, "SwimmingReportParser"},
    {new int[]{11}, "EllipticalMachineReportParser"}, {new int[]{12}, "YogaReportParser"},
    {new int[]{13}, "RowingMachineReportParser"}, {new int[]{14}, "RopeSkippingReportParser"},
    {new int[]{15}, "HikingReportParser"}, {new int[]{16}, "HITrainingReportParser"},
    {new int[]{17}, "TriathlonReportParser"}, {new int[]{18}, "OrdinaryBallReportParser"},
    {new int[]{19}, "BasketballReportParser"}, {new int[]{20}, "GolfReportParser"},
    {new int[]{21}, "SkiReportParser"}, {new int[]{22}, "OutdoorStepReportParser"},
    {new int[]{23}, "OutdoorNoStepReportParser"}, {new int[]{24}, "RockClimbingReportParser"},
    {new int[]{25}, "DivingReportParser"},
  };
  public static void main(String[] a) throws Exception {
    StringBuilder sb = new StringBuilder("{\n \"reports\": [\n");
    for (int s = 0; s < SPORTS.length; s++) {
      String cls = (String) SPORTS[s][1];
      SportReportBaseParser p = (SportReportBaseParser) Class.forName("com.xiaomi.fit.fitness.parser.sport.report." + cls).getDeclaredConstructor().newInstance();
      sb.append("  {\"parser\": \"").append(cls).append("\", \"sports\": ").append(Arrays.toString((int[]) SPORTS[s][0])).append(", \"fields\": [\n");
      OneDimenDataType[] f = p.getReportDataTypeArray();
      for (int i = 0; i < f.length; i++) {
        OneDimenDataType t = f[i];
        sb.append("   [").append(t.type).append(",").append(t.byteCount).append(",").append(t.supportVersion).append(",")
          .append(t.isFloat ? 1 : 0).append(",").append(t.isUnsigned ? 1 : 0).append(",").append(t.countsType == null ? -1 : t.countsType).append(",");
        if (t.dependData == null) sb.append("null"); else sb.append("[").append(t.dependData.type).append(",").append(Arrays.toString(t.dependData.values)).append("]");
        sb.append("]").append(i + 1 < f.length ? ",\n" : "\n");
      }
      sb.append("  ]}").append(s + 1 < SPORTS.length ? ",\n" : "\n");
    }
    sb.append(" ],\n").append(records()).append(" \"others\": {\n");
    String[] others = {"fitness.parser.sport.gps.SportGpsParser", "fitness.parser.daily.WeightRecordParser",
        "fitness.parser.daily.AbnormalRecordParser", "fitness.parser.daily.EcgMeasureParser",
        "fitness.parser.daily.TemperatureMeasureParser"};
    boolean firstOther = true;
    for (String cls : others) {
      Object p = Class.forName("com.xiaomi.fit." + cls).getDeclaredConstructor().newInstance();
      for (java.lang.reflect.Field fld : p.getClass().getDeclaredFields()) {
        if (fld.getType() != OneDimenDataType[].class) continue;
        fld.setAccessible(true);
        OneDimenDataType[] f = (OneDimenDataType[]) fld.get(p);
        sb.append(firstOther ? "" : ",\n").append("  \"").append(cls.substring(cls.lastIndexOf('.') + 1)).append(".").append(fld.getName()).append("\": [");
        firstOther = false;
        for (int i = 0; i < f.length; i++) {
          OneDimenDataType t = f[i];
          sb.append(i == 0 ? "" : ", ").append("[").append(t.type).append(",").append(t.byteCount).append(",").append(t.supportVersion).append(",")
            .append(t.isFloat ? 1 : 0).append(",").append(t.isUnsigned ? 1 : 0).append(",").append(t.countsType == null ? -1 : t.countsType).append(",");
          if (t.dependData == null) sb.append("null"); else sb.append("[").append(t.dependData.type).append(",").append(Arrays.toString(t.dependData.values)).append("]");
          sb.append("]");
        }
        sb.append("]");
      }
    }
    sb.append("\n },\n \"validity\": {\n");
    // sport file kinds 0 record, 1 report, 2 gps for sport types 1..26, versions 1..30
    boolean first = true;
    for (int kind = 0; kind <= 2; kind++) for (int sport = 1; sport <= 26; sport++) {
      List<String> vals = new ArrayList<>();
      for (int v = 1; v <= 30; v++) {
        int len = FitnessDataValidity.INSTANCE.getDataValidityLen(new FitnessDataId(1, sport, 0, kind, v));
        if (len >= 0) vals.add("\"" + v + "\": " + len);
      }
      if (vals.isEmpty()) continue;
      sb.append(first ? "" : ",\n").append("  \"sport_").append(sport).append("_").append(kind).append("\": {").append(String.join(", ", vals)).append("}");
      first = false;
    }
    sb.append("\n }\n}\n");
    System.out.print(sb);
  }
}
