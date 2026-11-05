package java.lang;

public final class Character {
    public static final int MIN_VALUE = 0x0000;
    public static final int MAX_VALUE = 0xffff;

    private final char value;

    public Character(char value) { this.value = value; }
    public char charValue() { return value; }
    public static Character valueOf(char c) { return new Character(c); }

    public static boolean isDigit(char c) { return c >= '0' && c <= '9'; }
    public static boolean isLetter(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    }
    public static boolean isLetterOrDigit(char c) {
        return isLetter(c) || isDigit(c);
    }
    public static boolean isWhitespace(char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
               c == '\f' || c == 0x0b;
    }
    public static boolean isUpperCase(char c) { return c >= 'A' && c <= 'Z'; }
    public static boolean isLowerCase(char c) { return c >= 'a' && c <= 'z'; }

    public static char toUpperCase(char c) {
        return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    }
    public static char toLowerCase(char c) {
        return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }

    public static int digit(char ch, int radix) {
        int d;
        if (radix < 2 || radix > 36) return -1;
        if (ch >= '0' && ch <= '9') d = ch - '0';
        else if (ch >= 'a' && ch <= 'z') d = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'Z') d = ch - 'A' + 10;
        else return -1;
        return d < radix ? d : -1;
    }
    public static int digit(int codePoint, int radix) {
        return digit((char)codePoint, radix);
    }

    public boolean equals(Object obj) {
        return obj instanceof Character &&
               ((Character)obj).value == value;
    }
    public int hashCode() { return value; }
    public String toString() { return String.valueOf(value); }
}
