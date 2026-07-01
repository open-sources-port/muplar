package android.content;

public final class ComponentName {
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
    @Override public String toString() { return flattenToString(); }
}
