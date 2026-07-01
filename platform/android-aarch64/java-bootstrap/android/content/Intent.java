package android.content;

public class Intent {
    public static final String ACTION_MAIN = "android.intent.action.MAIN";
    public static final String CATEGORY_HOME = "android.intent.category.HOME";
    public static final String CATEGORY_LAUNCHER = "android.intent.category.LAUNCHER";
    public static final int FLAG_ACTIVITY_NEW_TASK = 0x10000000;
    public static final int FLAG_ACTIVITY_RESET_TASK_IF_NEEDED = 0x00200000;
    public static final String ACTION_USER_UNLOCKED = "android.intent.action.USER_UNLOCKED";
    public static final String ACTION_PACKAGE_ADDED = "android.intent.action.PACKAGE_ADDED";
    public static final String ACTION_PACKAGE_REMOVED = "android.intent.action.PACKAGE_REMOVED";
    public static final String ACTION_PACKAGE_CHANGED = "android.intent.action.PACKAGE_CHANGED";
    private String pkg;
    private String cls;
    private String action;
    private final java.util.Set<String> categories =
        new java.util.LinkedHashSet<String>();
    private int flags;

    public Intent() {}
    public Intent(String action) { this.action = action; }
    public Intent(Context context, Class<?> type) {
        setClass(context, type);
    }
    public Intent(Intent source) {
        if (source == null) return;
        pkg = source.pkg;
        cls = source.cls;
        action = source.action;
        flags = source.flags;
        categories.addAll(source.categories);
    }
    public String getAction() { return action; }
    public Intent setAction(String value) { action = value; return this; }
    public Intent setPackage(String packageName) { pkg = packageName; return this; }
    public String getPackage() { return pkg; }
    public Intent addCategory(String category) {
        if (category != null) categories.add(category);
        return this;
    }
    public Intent removeCategory(String category) { categories.remove(category); return this; }
    public java.util.Set<String> getCategories() {
        return categories.isEmpty() ? null
            : java.util.Collections.unmodifiableSet(categories);
    }
    public Intent setFlags(int value) { flags = value; return this; }
    public Intent addFlags(int value) { flags |= value; return this; }
    public int getFlags() { return flags; }
    public String resolveTypeIfNeeded(ContentResolver resolver) { return null; }

    public Intent setClassName(String pkg, String cls) {
        this.pkg = pkg;
        this.cls = cls;
        return this;
    }
    public Intent setClass(Context context, Class<?> type) {
        return setClassName(context == null ? "" : context.getPackageName(),
            type == null ? "" : type.getName());
    }

    public String getComponentPackage() {
        return pkg;
    }

    public String getComponentClass() {
        return cls;
    }

    public ComponentName getComponent() {
        return pkg == null || cls == null ? null : new ComponentName(pkg, cls);
    }

    public Intent setComponent(ComponentName component) {
        if (component == null) {
            pkg = null;
            cls = null;
        } else {
            pkg = component.getPackageName();
            cls = component.getClassName();
        }
        return this;
    }
}
