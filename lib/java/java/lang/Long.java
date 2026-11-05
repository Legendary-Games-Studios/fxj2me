package java.lang;

public final class Long {
    public static final long MAX_VALUE = 0x7fffffffffffffffL;
    public static final long MIN_VALUE = 0x8000000000000000L;

    private final long value;

    public Long(long value) { this.value = value; }
    public long longValue() { return value; }
    public int intValue() { return (int)value; }
    public static Long valueOf(long l) { return new Long(l); }

    public static String toString(long i) { return toString(i, 10); }
    public static String toString(long i, int radix) {
        if (radix < 2 || radix > 36) radix = 10;
        if (i == MIN_VALUE) return "-9223372036854775808";
        boolean neg = i < 0;
        char[] buf = new char[65];
        int p = 65;
        long v = neg ? -i : i;
        while (v != 0 || p == 65) {
            int d = (int)(v % radix);
            buf[--p] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
            v /= radix;
        }
        if (neg) buf[--p] = '-';
        return new String(buf, p, 65 - p);
    }

    public static long parseLong(String s) throws NumberFormatException {
        return parseLong(s, 10);
    }
    public static long parseLong(String s, int radix)
            throws NumberFormatException {
        if (s == null || s.length() == 0)
            throw new NumberFormatException("empty");
        int i = 0;
        boolean neg = false;
        char first = s.charAt(0);
        if (first == '-') { neg = true; i = 1; }
        else if (first == '+') { i = 1; }
        if (i >= s.length()) throw new NumberFormatException(s);
        long result = 0;
        for (; i < s.length(); i++) {
            int d = Character.digit(s.charAt(i), radix);
            if (d < 0) throw new NumberFormatException(s);
            if (result < Long.MIN_VALUE / radix)
                throw new NumberFormatException(s);
            result = result * radix - d;
        }
        return neg ? result : -result;
    }

    public boolean equals(Object obj) {
        return obj instanceof Long && ((Long)obj).value == value;
    }
    public int hashCode() { return (int)(value ^ (value >>> 32)); }
    public String toString() { return toString(value); }
}
