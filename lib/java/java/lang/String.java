package java.lang;

public final class String {
    /* Layout contract with the VM: value is the UTF-16 payload, count the
     * number of chars. The VM builds literals via these fields. */
    private char[] value;
    private int count;

    public String() { value = new char[0]; count = 0; }
    public String(char[] data) {
        this(data, 0, data.length);
    }
    public String(char[] data, int offset, int count) {
        if (offset < 0 || count < 0 || offset + count > data.length)
            throw new StringIndexOutOfBoundsException(offset + count);
        value = new char[count];
        System.arraycopy(data, offset, value, 0, count);
        this.count = count;
    }
    public String(String other) {
        this.value = other.value;
        this.count = other.count;
    }

    public int length() { return count; }
    public boolean isEmpty() { return count == 0; }

    public char charAt(int index) {
        if (index < 0 || index >= count)
            throw new StringIndexOutOfBoundsException(index);
        return value[index];
    }

    public void getChars(int srcBegin, int srcEnd, char[] dst, int dstBegin) {
        if (srcBegin < 0 || srcEnd > count || srcBegin > srcEnd)
            throw new StringIndexOutOfBoundsException(srcEnd);
        System.arraycopy(value, srcBegin, dst, dstBegin, srcEnd - srcBegin);
    }

    public byte[] getBytes() {
        byte[] b = new byte[count];
        for (int i = 0; i < count; i++) b[i] = (byte)value[i];
        return b;
    }

    public boolean equals(Object other) {
        if (this == other) return true;
        if (!(other instanceof String)) return false;
        String s = (String)other;
        if (s.count != count) return false;
        for (int i = 0; i < count; i++)
            if (value[i] != s.value[i]) return false;
        return true;
    }

    public boolean equalsIgnoreCase(String s) {
        if (s == null || s.count != count) return false;
        for (int i = 0; i < count; i++) {
            char a = value[i], b = s.value[i];
            if (a != b) {
                if (Character.toUpperCase(a) != Character.toUpperCase(b))
                    return false;
            }
        }
        return true;
    }

    public int compareTo(String s) {
        int n = count < s.count ? count : s.count;
        for (int i = 0; i < n; i++) {
            int d = value[i] - s.value[i];
            if (d != 0) return d;
        }
        return count - s.count;
    }

    public int hashCode() {
        int h = 0;
        for (int i = 0; i < count; i++) h = 31 * h + value[i];
        return h;
    }

    public boolean startsWith(String prefix) { return startsWith(prefix, 0); }
    public boolean startsWith(String prefix, int toffset) {
        if (toffset < 0 || toffset + prefix.count > count) return false;
        for (int i = 0; i < prefix.count; i++)
            if (value[toffset + i] != prefix.value[i]) return false;
        return true;
    }
    public boolean endsWith(String suffix) {
        return startsWith(suffix, count - suffix.count);
    }

    public int indexOf(int ch) { return indexOf(ch, 0); }
    public int indexOf(int ch, int from) {
        if (from < 0) from = 0;
        for (int i = from; i < count; i++)
            if (value[i] == (char)ch) return i;
        return -1;
    }
    public int lastIndexOf(int ch) {
        for (int i = count - 1; i >= 0; i--)
            if (value[i] == (char)ch) return i;
        return -1;
    }
    public int indexOf(String s) { return indexOf(s, 0); }
    public int indexOf(String s, int from) {
        if (from < 0) from = 0;
        outer:
        for (int i = from; i + s.count <= count; i++) {
            for (int j = 0; j < s.count; j++)
                if (value[i + j] != s.value[j]) continue outer;
            return i;
        }
        return -1;
    }

    public String substring(int begin) { return substring(begin, count); }
    public String substring(int begin, int end) {
        if (begin < 0 || end > count || begin > end)
            throw new StringIndexOutOfBoundsException(end);
        if (begin == 0 && end == count) return this;
        char[] r = new char[end - begin];
        System.arraycopy(value, begin, r, 0, end - begin);
        return new String(r);
    }

    public String concat(String s) {
        if (s.count == 0) return this;
        char[] r = new char[count + s.count];
        System.arraycopy(value, 0, r, 0, count);
        System.arraycopy(s.value, 0, r, count, s.count);
        return new String(r);
    }

    public String replace(char oldChar, char newChar) {
        char[] r = null;
        for (int i = 0; i < count; i++) {
            if (value[i] == oldChar) {
                if (r == null) {
                    r = new char[count];
                    System.arraycopy(value, 0, r, 0, count);
                }
                r[i] = newChar;
            }
        }
        return r == null ? this : new String(r);
    }

    public boolean regionMatches(int toffset, String other, int ooffset,
                                 int len) {
        if (ooffset < 0 || toffset < 0 || toffset + len > count ||
            ooffset + len > other.count)
            return false;
        for (int i = 0; i < len; i++)
            if (value[toffset + i] != other.value[ooffset + i]) return false;
        return true;
    }
    public boolean regionMatches(boolean ignoreCase, int toffset, String other,
                                 int ooffset, int len) {
        if (!ignoreCase) return regionMatches(toffset, other, ooffset, len);
        if (ooffset < 0 || toffset < 0 || toffset + len > count ||
            ooffset + len > other.count)
            return false;
        for (int i = 0; i < len; i++) {
            char a = value[toffset + i], b = other.value[ooffset + i];
            if (a != b &&
                Character.toUpperCase(a) != Character.toUpperCase(b))
                return false;
        }
        return true;
    }

    public String trim() {
        int b = 0, e = count;
        while (b < e && value[b] <= ' ') b++;
        while (e > b && value[e - 1] <= ' ') e--;
        return (b == 0 && e == count) ? this : substring(b, e);
    }

    public char[] toCharArray() {
        char[] r = new char[count];
        System.arraycopy(value, 0, r, 0, count);
        return r;
    }

    public String toString() { return this; }

    public static String valueOf(boolean b) { return b ? "true" : "false"; }
    public static String valueOf(char c) { return new String(new char[] { c }); }
    public static String valueOf(char[] data) { return new String(data); }
    public static String valueOf(char[] data, int offset, int count) {
        return new String(data, offset, count);
    }
    public static String valueOf(int i) { return Integer.toString(i); }
    public static String valueOf(long l) { return Long.toString(l); }
    public static String valueOf(float f) { return Float.toString(f); }
    public static String valueOf(double d) { return Double.toString(d); }
    public static String valueOf(Object obj) {
        return obj == null ? "null" : obj.toString();
    }
}
