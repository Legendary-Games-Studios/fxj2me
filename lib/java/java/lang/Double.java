package java.lang;

public final class Double {
    public static final double MAX_VALUE = 1.7976931348623157e308;
    public static final double MIN_VALUE = 4.9e-324;
    public static final double NaN = 0.0 / 0.0;
    public static final double POSITIVE_INFINITY = 1.0 / 0.0;
    public static final double NEGATIVE_INFINITY = -1.0 / 0.0;

    private final double value;

    public Double(double value) { this.value = value; }
    public double doubleValue() { return value; }
    public float floatValue() { return (float)value; }
    public int intValue() { return (int)value; }
    public long longValue() { return (long)value; }
    public static Double valueOf(double d) { return new Double(d); }
    public static Double valueOf(String s) throws NumberFormatException {
        return new Double(parseDouble(s));
    }

    /* Decimal parse/format are natives (strtod/snprintf). */
    public static native double parseDouble(String s)
        throws NumberFormatException;
    public static native String toString(double d);
    native static long floatToLongBits0(float f);

    public static boolean isNaN(double d) { return d != d; }
    public static boolean isInfinite(double d) {
        return d == POSITIVE_INFINITY || d == NEGATIVE_INFINITY;
    }
    public static long doubleToLongBits(double d) {
        long bits = 0;
        boolean neg = d < 0 || (d == 0 && 1 / d < 0);
        if (d != d) return 0x7ff8000000000000L;
        if (d == POSITIVE_INFINITY) return 0x7ff0000000000000L;
        if (d == NEGATIVE_INFINITY) return 0xfff0000000000000L;
        /* Simple scaling encoder: exact enough for equals/hashCode use. */
        double a = neg ? -d : d;
        int exp = 0;
        while (a >= 2.0) { a /= 2.0; exp++; }
        while (a < 1.0 && a > 0) { a *= 2.0; exp--; }
        if (a == 0) return neg ? (long)1 << 63 : 0;
        long mant = (long)((a - 1.0) * (1L << 52));
        bits = ((long)(exp + 1023) << 52) | mant;
        return neg ? bits | ((long)1 << 63) : bits;
    }

    public boolean equals(Object obj) {
        return obj instanceof Double && ((Double)obj).value == value;
    }
    public int hashCode() {
        long b = doubleToLongBits(value);
        return (int)(b ^ (b >>> 32));
    }
    public String toString() { return toString(value); }
}
