package java.lang;

public final class StringBuilder {
    private final StringBuffer sb = new StringBuffer();

    public StringBuilder() { }
    public StringBuilder(int capacity) { sb.ensure0(capacity); }
    public StringBuilder(String s) { sb.append(s); }
    public int length() { return sb.length(); }
    public StringBuilder append(Object obj) { sb.append(obj); return this; }
    public StringBuilder append(String s) { sb.append(s); return this; }
    public StringBuilder append(char[] str) { sb.append(str); return this; }
    public StringBuilder append(char ch) { sb.append(ch); return this; }
    public StringBuilder append(boolean b) { sb.append(b); return this; }
    public StringBuilder append(int i) { sb.append(i); return this; }
    public StringBuilder append(long l) { sb.append(l); return this; }
    public StringBuilder append(float f) { sb.append(f); return this; }
    public StringBuilder append(double d) { sb.append(d); return this; }
    public StringBuilder insert(int offset, String s) { sb.insert(offset, s); return this; }
    public StringBuilder delete(int start, int end) { sb.delete(start, end); return this; }
    public StringBuilder reverse() { sb.reverse(); return this; }
    public String toString() { return sb.toString(); }
}
