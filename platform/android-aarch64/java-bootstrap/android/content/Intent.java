package android.content;

public class Intent {
    public static final String ACTION_USER_UNLOCKED = "android.intent.action.USER_UNLOCKED";
    public static final String ACTION_PACKAGE_ADDED = "android.intent.action.PACKAGE_ADDED";
    public static final String ACTION_PACKAGE_REMOVED = "android.intent.action.PACKAGE_REMOVED";
    public static final String ACTION_PACKAGE_CHANGED = "android.intent.action.PACKAGE_CHANGED";
    private String pkg;
    private String cls;
    private String action;

    public Intent() {}
    public Intent(String action) { this.action = action; }
    public String getAction() { return action; }
    public Intent setAction(String value) { action = value; return this; }
    public Intent setPackage(String packageName) { pkg = packageName; return this; }
    public String getPackage() { return pkg; }

    public Intent setClassName(String pkg, String cls) {
        this.pkg = pkg;
        this.cls = cls;
        return this;
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
