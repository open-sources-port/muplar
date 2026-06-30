package android.content.pm;

public class ApplicationInfo {
    public String packageName;
    public String name;
    public String sourceDir;
    public String launchActivity;
    public String iconPath;

    public CharSequence loadLabel(PackageManager packageManager) {
        return name == null || name.isEmpty() ? packageName : name;
    }
}
