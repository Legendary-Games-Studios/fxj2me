package java.lang;

public final class Boolean {
    public static final Boolean TRUE = new Boolean(true);
    public static final Boolean FALSE = new Boolean(false);

    private final boolean value;

    public Boolean(boolean value) { this.value = value; }
    public boolean booleanValue() { return value; }
    public static Boolean valueOf(boolean b) { return b ? TRUE : FALSE; }
    public static boolean valueOf(String s) {
        return s != null && s.equalsIgnoreCase("true");
    }
    public static String toString(boolean b) { return b ? "true" : "false"; }

    public boolean equals(Object obj) {
        return obj instanceof Boolean &&
               ((Boolean)obj).value == value;
    }
    public int hashCode() { return value ? 1231 : 1237; }
    public String toString() { return toString(value); }
}
