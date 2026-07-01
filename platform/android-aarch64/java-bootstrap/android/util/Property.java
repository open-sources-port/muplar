package android.util;

public abstract class Property<T, V> {
    private final Class<V> type;
    private final String name;
    public Property(Class<V> type, String name) {
        this.type = type;
        this.name = name;
    }
    public abstract V get(T object);
    public abstract void set(T object, V value);
    public String getName() { return name; }
    public Class<V> getType() { return type; }
    public boolean isReadOnly() { return false; }
    @Override public String toString() {
        return "Property{" + name + " type=" + type.getName() + "}";
    }
}
