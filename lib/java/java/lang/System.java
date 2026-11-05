package java.lang;

public final class System {
    private System() { }

    public static native void arraycopy(Object src, int srcPos, Object dest,
                                        int destPos, int length);
    public static native long currentTimeMillis();
    public static native void gc();
    public static native int identityHashCode(Object x);
    public static native void exit(int status);
    public static native String getProperty(String key);
}
