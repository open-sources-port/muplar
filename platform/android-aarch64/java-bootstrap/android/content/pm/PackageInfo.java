package android.content.pm;

public class PackageInfo {
    public String packageName;
    public ApplicationInfo applicationInfo;
    public long firstInstallTime;
    public long lastUpdateTime;
    public int versionCode;
    public long getLongVersionCode() { return versionCode; }
}
