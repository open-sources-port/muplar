package android.content.pm;

import android.graphics.drawable.Drawable;

public class ResolveInfo {
    public ActivityInfo activityInfo;
    public int priority;
    public boolean isDefault;
    public CharSequence loadLabel(PackageManager packageManager) {
        return activityInfo == null ? null : activityInfo.loadLabel(packageManager);
    }
    public Drawable loadIcon(PackageManager packageManager) {
        return activityInfo == null || activityInfo.applicationInfo == null ? null
            : activityInfo.applicationInfo.loadIcon(packageManager);
    }
}
