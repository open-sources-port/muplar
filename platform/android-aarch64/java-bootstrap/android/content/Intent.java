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
    private android.net.Uri data;
    private final java.util.Set<String> categories =
        new java.util.LinkedHashSet<String>();
    private int flags;
    private android.graphics.Rect sourceBounds;
    private final java.util.Map<String, Object> extras =
        new java.util.HashMap<String, Object>();

    public Intent() {}
    public Intent(String action) { this.action = action; }
    public Intent(String action, android.net.Uri uri) {
        this.action = action;
        this.data = uri;
    }
    public Intent(Context context, Class<?> type) {
        setClass(context, type);
    }
    public Intent(Intent source) {
        if (source == null) return;
        pkg = source.pkg;
        cls = source.cls;
        action = source.action;
        data = source.data;
        flags = source.flags;
        sourceBounds = source.sourceBounds == null
            ? null : new android.graphics.Rect(source.sourceBounds);
        categories.addAll(source.categories);
        extras.putAll(source.extras);
    }
    public static Intent parseUri(String uri, int flags)
            throws java.net.URISyntaxException {
        if (uri == null) throw new java.net.URISyntaxException("", "null intent URI");
        Intent intent = new Intent();
        int marker = uri.indexOf("#Intent;");
        if (marker < 0) return intent.setAction(ACTION_MAIN);
        String[] parts = uri.substring(marker + 8).split(";");
        for (String part : parts) {
            if (part.startsWith("action=")) intent.setAction(part.substring(7));
            else if (part.startsWith("package=")) intent.setPackage(part.substring(8));
            else if (part.startsWith("category=")) intent.addCategory(part.substring(9));
            else if (part.startsWith("component=")) {
                String component = part.substring(10);
                int slash = component.indexOf('/');
                if (slash > 0) {
                    String packageName = component.substring(0, slash);
                    String className = component.substring(slash + 1);
                    if (className.startsWith(".")) className = packageName + className;
                    intent.setClassName(packageName, className);
                }
            } else if (part.startsWith("launchFlags=")) {
                try { intent.setFlags(Integer.decode(part.substring(12))); }
                catch (NumberFormatException ignored) {}
            }
        }
        return intent;
    }
    public String getAction() { return action; }
    public Intent setAction(String value) { action = value; return this; }
    public android.net.Uri getData() { return data; }
    public Intent setData(android.net.Uri value) { data = value; return this; }
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
    public void setSourceBounds(android.graphics.Rect bounds) {
        sourceBounds = bounds == null ? null : new android.graphics.Rect(bounds);
    }
    public android.graphics.Rect getSourceBounds() {
        return sourceBounds == null ? null : new android.graphics.Rect(sourceBounds);
    }
    public Intent putExtra(String name, android.os.Parcelable value) {
        extras.put(name, value); return this;
    }
    public Intent putExtra(String name, String value) {
        extras.put(name, value); return this;
    }
    public Intent putExtra(String name, int value) {
        extras.put(name, Integer.valueOf(value)); return this;
    }
    public android.os.Parcelable getParcelableExtra(String name) {
        Object value = extras.get(name);
        return value instanceof android.os.Parcelable
            ? (android.os.Parcelable) value : null;
    }
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

    public String toUri(int uriFlags) {
        StringBuilder value = new StringBuilder("intent:#Intent;");
        if (action != null) value.append("action=").append(action).append(';');
        if (pkg != null) value.append("package=").append(pkg).append(';');
        if (pkg != null && cls != null) {
            value.append("component=").append(pkg).append('/').append(cls).append(';');
        }
        for (String category : categories) {
            value.append("category=").append(category).append(';');
        }
        if (flags != 0) value.append("launchFlags=0x")
            .append(Integer.toHexString(flags)).append(';');
        return value.append("end").toString();
    }
}
