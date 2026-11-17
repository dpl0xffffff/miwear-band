package kotlin.jvm.internal;
public final class Intrinsics {
  public static void checkNotNullParameter(Object o, String n) {}
  public static void checkNotNull(Object o) {}
  public static void checkNotNull(Object o, String m) {}
  public static boolean areEqual(Object a, Object b) { return a == null ? b == null : a.equals(b); }
}
