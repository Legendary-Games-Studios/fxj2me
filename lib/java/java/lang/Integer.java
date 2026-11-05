package java.lang;

public final class Integer {
    public static final int MAX_VALUE = 0x7fffffff;
    public static final int MIN_VALUE = 0x80000000;

    private final int value;

    public Integer(int value) { this.value = value; }
    public int intValue() { return value; }
    public long longValue() { return value; }
    public static Integer valueOf(int i) { return new Integer(i); }

    public static String toString(int i) { return toString(i, 10); }
    public static String toString(int i, int radix) {
        if (radix < 2 || radix > 36) radix = 10;
        if (i == MIN_VALUE) return "-2147483648";
        boolean neg = i < 0;
        char[] buf = new char[33];
        int p = 33;
        long v = neg ? -(long)i : i;
        while (v != 0 || p == 33) {
            int d = (int)(v % radix);
            buf[--p] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
            v /= radix;
        }
        if (neg) buf[--p] = '-';
        return new String(buf, p, 33 - p);
    }
    public static String toHexString(int i) {
        return toUnsignedString(i, 4);
    }
    public static String toOctalString(int i) {
        return toUnsignedString(i, 3);
    }
    public static String toBinaryString(int i) {
        return toUnsignedString(i, 1);
    }
    private static String toUnsignedString(int i, int shift) {
        char[] buf = new char[32];
        int p = 32;
        int mask = (1 << shift) - 1;
        do {
            buf[--p] = "0123456789abcdef".charAt(i & mask);
            i >>>= shift;
        } while (i != 0);
        return new String(buf, p, 32 - p);
    }

    public static int parseInt(String s) throws NumberFormatException {
        return parseInt(s, 10);
    }
    public static int parseInt(String s, int radix)
            throws NumberFormatException {
        if (s == null || s.length() == 0)
            throw new NumberFormatException("empty");
        int i = 0;
        boolean neg = false;
        char first = s.charAt(0);
        if (first == '-') {
            neg = true;
            i = 1;
        } else if (first == '+') {
            i = 1;
        }
        if (i >= s.length())
            throw new NumberFormatException(s);
        long result = 0;
        for (; i < s.length(); i++) {
            int d = Character.digit(s.charAt(i), radix);
            if (d < 0) throw new NumberFormatException(s);
            result = result * radix - d;
            if (result < Integer.MIN_VALUE)
                throw new NumberFormatException(s);
        }
        return (int)(neg ? result : -result);
    }
    public static Integer valueOf(String s) throws NumberFormatException {
        return new Integer(parseInt(s));
    }

    public boolean equals(Object obj) {
        return obj instanceof Integer && ((Integer)obj).value == value;
    }
    public int hashCode() { return value; }
    public String toString() { return toString(value); }
}
