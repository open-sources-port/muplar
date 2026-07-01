package android.content.pm;

public class ActivityInfo {
    public ApplicationInfo applicationInfo;
    public String packageName;
    public String name;
    public boolean enabled = true;
    public int configChanges;
    public CharSequence loadLabel(PackageManager packageManager) {
        return applicationInfo == null ? name : applicationInfo.loadLabel(packageManager);
    }
}
