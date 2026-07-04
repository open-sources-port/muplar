package android.content.pm;

import android.content.ComponentName;
import android.graphics.drawable.Drawable;
import android.os.UserHandle;

public class LauncherActivityInfo {
    private final ApplicationInfo applicationInfo;
    private final PackageManager packageManager;
    private final UserHandle user;
    LauncherActivityInfo(ApplicationInfo info, PackageManager packageManager,
                         UserHandle user) {
        this.applicationInfo = info;
        this.packageManager = packageManager;
        this.user = user;
    }
    public ComponentName getComponentName() {
        return new ComponentName(applicationInfo.packageName,
            applicationInfo.launchActivity);
    }
    public UserHandle getUser() { return user; }
    public CharSequence getLabel() { return applicationInfo.loadLabel(packageManager); }
    public Drawable getIcon(int density) { return applicationInfo.loadIcon(packageManager); }
    public ApplicationInfo getApplicationInfo() { return applicationInfo; }
    public float getLoadingProgress() { return 1.0f; }
    public ActivityInfo getActivityInfo() {
        ActivityInfo info = new ActivityInfo();
        info.applicationInfo = applicationInfo;
        info.packageName = applicationInfo.packageName;
        info.name = applicationInfo.launchActivity;
        return info;
    }
}
