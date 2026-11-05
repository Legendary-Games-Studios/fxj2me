package java.lang;

public final class Float {
    public static final float MAX_VALUE = 3.4028235e38f;
    public static final float MIN_VALUE = 1.4e-45f;
    public static final float NaN = 0.0f / 0.0f;
    public static final float POSITIVE_INFINITY = 1.0f / 0.0f;
    public static final float NEGATIVE_INFINITY = -1.0f / 0.0f;

    private final float value;

    public Float(float value) { this.value = value; }
    public Float(double value) { this.value = (float)value; }
    public float floatValue() { return value; }
    public double doubleValue() { return value; }
    public int intValue() { return (int)value; }
    public static Float valueOf(float f) { return new Float(f); }
    public static Float valueOf(String s) throws NumberFormatException {
        return new Float(parseFloat(s));
    }

    public static float parseFloat(String s) throws NumberFormatException {
        double d = Double.parseDouble(s);
        return (float)d;
    }
    public static boolean isNaN(float f) { return f != f; }
    public static boolean isInfinite(float f) {
        return f == POSITIVE_INFINITY || f == NEGATIVE_INFINITY;
    }
    public static int floatToIntBits(float f) {
        return (int)Double.floatToLongBits0(f);
    }
    public static String toString(float f) { return Double.toString(f); }

    public boolean equals(Object obj) {
        return obj instanceof Float && ((Float)obj).value == value;
    }
    public int hashCode() { return floatToIntBits(value); }
    public String toString() { return toString(value); }
}
