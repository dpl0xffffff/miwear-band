import com.xiaomi.fit.fitness.parser.base.FitnessDataBaseParser.*;
import com.xiaomi.fit.fitness.parser.sport.base.SportReportBaseParser;
import com.xiaomi.fit.data.common.data.mi.*;
import java.util.*;
public class DumpTables {
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
    sb.append(" ],\n \"others\": {\n");
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
