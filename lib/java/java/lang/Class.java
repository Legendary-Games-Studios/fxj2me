package java.lang;

public final class Class<T> {
    private String name;
    private Class() { }
    public String getName() { return name; }
    public String toString() { return "class " + name; }
    public boolean isInstance(Object obj) {
        if (obj == null) return false;
        return isAssignableFrom(obj.getClass());
    }
    public native boolean isAssignableFrom(Class<?> cls);
    public native T newInstance() throws InstantiationException,
        IllegalAccessException;
}
