package android.content;

public final class ComponentName implements Comparable<ComponentName> {
    private final String packageName;
    private final String className;
    public ComponentName(String packageName, String className) {
        this.packageName = packageName;
        this.className = className;
    }
    public ComponentName(Context context, Class<?> type) {
        this(context == null ? "" : context.getPackageName(),
            type == null ? "" : type.getName());
    }
    public String getPackageName() { return packageName; }
    public String getClassName() { return className; }
    public String flattenToString() { return packageName + "/" + className; }
    public String flattenToShortString() {
        String shortClass = className.startsWith(packageName)
            ? className.substring(packageName.length()) : className;
        return packageName + "/" + shortClass;
    }
    public static ComponentName unflattenFromString(String value) {
        if (value == null) return null;
        int slash = value.indexOf('/');
        if (slash <= 0 || slash == value.length() - 1) return null;
        String packageName = value.substring(0, slash);
        String className = value.substring(slash + 1);
        if (className.startsWith(".")) className = packageName + className;
        return new ComponentName(packageName, className);
    }
    public int compareTo(ComponentName other) {
        int packageOrder = packageName.compareTo(other.packageName);
        return packageOrder != 0 ? packageOrder : className.compareTo(other.className);
    }
    @Override public boolean equals(Object other) {
        if (!(other instanceof ComponentName)) return false;
        ComponentName component = (ComponentName) other;
        return packageName.equals(component.packageName) &&
            className.equals(component.className);
    }
    @Override public int hashCode() { return 31 * packageName.hashCode() + className.hashCode(); }
    @Override public String toString() { return flattenToString(); }
}
