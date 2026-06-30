package android.content.pm;

import android.graphics.drawable.Drawable;

public class ApplicationInfo {
    public String packageName;
    public String name;
    public String sourceDir;
    public String launchActivity;
    public String iconPath;

    public CharSequence loadLabel(PackageManager packageManager) {
        return name == null || name.isEmpty() ? packageName : name;
    }

    public Drawable loadIcon(PackageManager packageManager) {
        return packageManager.getApplicationIcon(this);
    }
}
