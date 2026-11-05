package java.lang;

public final class StringBuffer {
    private char[] value;
    private int count;

    public StringBuffer() { value = new char[16]; }
    public StringBuffer(int capacity) {
        value = new char[capacity > 0 ? capacity : 16];
    }
    public StringBuffer(String s) {
        value = new char[s.length() + 16];
        s.getChars(0, s.length(), value, 0);
        count = s.length();
    }

    public int length() { return count; }
    public int capacity() { return value.length; }

    void ensure0(int minimum) { ensure(minimum); }
    private void ensure(int minimum) {
        if (minimum > value.length) {
            int cap = value.length * 2 + 2;
            if (cap < minimum) cap = minimum;
            char[] nv = new char[cap];
            System.arraycopy(value, 0, nv, 0, count);
            value = nv;
        }
    }

    public char charAt(int index) {
        if (index < 0 || index >= count)
            throw new StringIndexOutOfBoundsException(index);
        return value[index];
    }
    public void setCharAt(int index, char ch) {
        if (index < 0 || index >= count)
            throw new StringIndexOutOfBoundsException(index);
        value[index] = ch;
    }

    public StringBuffer append(Object obj) { return append(String.valueOf(obj)); }
    public StringBuffer append(String s) {
        if (s == null) s = "null";
        int n = s.length();
        ensure(count + n);
        s.getChars(0, n, value, count);
        count += n;
        return this;
    }
    public StringBuffer append(char[] str) {
        ensure(count + str.length);
        System.arraycopy(str, 0, value, count, str.length);
        count += str.length;
        return this;
    }
    public StringBuffer append(char ch) {
        ensure(count + 1);
        value[count++] = ch;
        return this;
    }
    public StringBuffer append(boolean b) { return append(String.valueOf(b)); }
    public StringBuffer append(int i) { return append(Integer.toString(i)); }
    public StringBuffer append(long l) { return append(Long.toString(l)); }
    public StringBuffer append(float f) { return append(Float.toString(f)); }
    public StringBuffer append(double d) { return append(Double.toString(d)); }

    public StringBuffer insert(int offset, String s) {
        if (offset < 0 || offset > count)
            throw new StringIndexOutOfBoundsException(offset);
        if (s == null) s = "null";
        int n = s.length();
        ensure(count + n);
        System.arraycopy(value, offset, value, offset + n, count - offset);
        s.getChars(0, n, value, offset);
        count += n;
        return this;
    }
    public StringBuffer insert(int offset, char ch) {
        return insert(offset, String.valueOf(ch));
    }
    public StringBuffer insert(int offset, int i) {
        return insert(offset, Integer.toString(i));
    }

    public StringBuffer delete(int start, int end) {
        if (start < 0 || end > count || start > end)
            throw new StringIndexOutOfBoundsException(end);
        int n = end - start;
        System.arraycopy(value, end, value, start, count - end);
        count -= n;
        return this;
    }
    public StringBuffer deleteCharAt(int index) {
        return delete(index, index + 1);
    }

    public StringBuffer reverse() {
        for (int i = 0, j = count - 1; i < j; i++, j--) {
            char t = value[i];
            value[i] = value[j];
            value[j] = t;
        }
        return this;
    }

    public void setLength(int newLength) {
        if (newLength < 0)
            throw new StringIndexOutOfBoundsException(newLength);
        ensure(newLength);
        for (int i = count; i < newLength; i++) value[i] = '\0';
        count = newLength;
    }

    public String toString() {
        char[] r = new char[count];
        System.arraycopy(value, 0, r, 0, count);
        return new String(r);
    }
}
