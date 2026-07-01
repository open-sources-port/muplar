package android.content.pm;

import android.graphics.drawable.Drawable;

public class ApplicationInfo {
    public static final int FLAG_DEBUGGABLE = 0x2;
    public String packageName;
    public String name;
    public String sourceDir;
    public String launchActivity;
    public String iconPath;
    public int flags;
    public int targetSdkVersion = 35;
    public int uid = 10000;
    public boolean enabled = true;
    private boolean enableOnBackInvokedCallback;

    public void setEnableOnBackInvokedCallback(boolean enabled) {
        enableOnBackInvokedCallback = enabled;
    }
    public boolean isOnBackInvokedCallbackEnabled() {
        return enableOnBackInvokedCallback;
    }

    public CharSequence loadLabel(PackageManager packageManager) {
        return name == null || name.isEmpty() ? packageName : name;
    }

    public Drawable loadIcon(PackageManager packageManager) {
        return packageManager.getApplicationIcon(this);
    }
}
