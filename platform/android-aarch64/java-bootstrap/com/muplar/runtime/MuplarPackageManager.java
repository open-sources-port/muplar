package com.muplar.runtime;

import android.content.ComponentName;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.ActivityInfo;
import android.content.pm.ApplicationInfo;
import android.content.pm.ChangedPackages;
import android.content.pm.FeatureInfo;
import android.content.pm.InstrumentationInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageInstaller;
import android.content.pm.PackageManager;
import android.content.pm.PermissionGroupInfo;
import android.content.pm.PermissionInfo;
import android.content.pm.ProviderInfo;
import android.content.pm.ResolveInfo;
import android.content.pm.ServiceInfo;
import android.content.pm.SharedLibraryInfo;
import android.content.pm.VersionedPackage;
import android.content.res.Resources;
import android.content.res.XmlResourceParser;
import android.graphics.Rect;
import android.graphics.drawable.Drawable;
import android.os.UserHandle;
import java.util.Collections;
import java.util.List;

public final class MuplarPackageManager extends PackageManager {
    private final String packageName;
    private final ApplicationInfo applicationInfo;
    private final Resources resources;

    public MuplarPackageManager(String packageName,
                                ApplicationInfo applicationInfo,
                                Resources resources) {
        this.packageName = packageName;
        this.applicationInfo = applicationInfo;
        this.resources = resources;
    }

    @Override public void addPackageToPreferred(String packageName) {}
    @Override public boolean addPermission(PermissionInfo info) { return false; }
    @Override public boolean addPermissionAsync(PermissionInfo info) { return false; }
    @Override public void addPreferredActivity(IntentFilter filter, int match,
            ComponentName[] set, ComponentName activity) {}
    @Override public boolean canRequestPackageInstalls() { return false; }
    @Override public String[] canonicalToCurrentPackageNames(String[] names) { return names; }
    @Override public int checkPermission(String permName, String pkgName) {
        return PERMISSION_GRANTED;
    }
    @Override public int checkSignatures(int uid1, int uid2) { return SIGNATURE_MATCH; }
    @Override public int checkSignatures(String pkg1, String pkg2) { return SIGNATURE_MATCH; }
    @Override public void clearInstantAppCookie() {}
    @Override public void clearPackagePreferredActivities(String packageName) {}
    @Override public String[] currentToCanonicalPackageNames(String[] names) { return names; }
    @Override public void extendVerificationTimeout(int id, int codeAtTimeout, long timeout) {}
    @Override public Drawable getActivityBanner(ComponentName activity) { return null; }
    @Override public Drawable getActivityBanner(Intent intent) { return null; }
    @Override public Drawable getActivityIcon(ComponentName activity) { return null; }
    @Override public Drawable getActivityIcon(Intent intent) { return null; }
    @Override public ActivityInfo getActivityInfo(ComponentName component, int flags)
            throws NameNotFoundException {
        if (component != null) {
            for (MuplarServices.InstalledPackage pkg :
                    MuplarServices.queryInstalledPackages()) {
                if (component.getPackageName().equals(pkg.packageName)
                    && component.getClassName().equals(pkg.activity)) {
                    return createActivityInfo(pkg);
                }
            }
        }
        throw new NameNotFoundException(component != null
            ? component.flattenToShortString() : "");
    }
    @Override public Drawable getActivityLogo(ComponentName activity) { return null; }
    @Override public Drawable getActivityLogo(Intent intent) { return null; }
    @Override public List<PermissionGroupInfo> getAllPermissionGroups(int flags) {
        return Collections.emptyList();
    }
    @Override public Drawable getApplicationBanner(ApplicationInfo info) { return null; }
    @Override public Drawable getApplicationBanner(String packageName) { return null; }
    @Override public int getApplicationEnabledSetting(String packageName) {
        return COMPONENT_ENABLED_STATE_DEFAULT;
    }
    @Override public Drawable getApplicationIcon(ApplicationInfo info) { return null; }
    @Override public Drawable getApplicationIcon(String packageName) { return null; }
    @Override public ApplicationInfo getApplicationInfo(String packageName, int flags)
            throws NameNotFoundException {
        if (this.packageName.equals(packageName)) return applicationInfo;
        for (MuplarServices.InstalledPackage pkg :
                MuplarServices.queryInstalledPackages()) {
            if (packageName.equals(pkg.packageName)) {
                return createApplicationInfo(pkg);
            }
        }
        throw new NameNotFoundException(packageName);
    }
    @Override public CharSequence getApplicationLabel(ApplicationInfo info) {
        return info != null && info.packageName != null ? info.packageName : packageName;
    }
    @Override public Drawable getApplicationLogo(ApplicationInfo info) { return null; }
    @Override public Drawable getApplicationLogo(String packageName) { return null; }
    @Override public ChangedPackages getChangedPackages(int sequenceNumber) { return null; }
    @Override public int getComponentEnabledSetting(ComponentName componentName) {
        return COMPONENT_ENABLED_STATE_DEFAULT;
    }
    @Override public Drawable getDefaultActivityIcon() { return null; }
    @Override public Drawable getDrawable(String packageName, int resId, ApplicationInfo appInfo) {
        try { return resources.getDrawable(resId, null); } catch (Throwable ignored) { return null; }
    }
    @Override public List<ApplicationInfo> getInstalledApplications(int flags) {
        java.util.ArrayList<ApplicationInfo> result =
            new java.util.ArrayList<ApplicationInfo>();
        result.add(applicationInfo);
        for (MuplarServices.InstalledPackage pkg :
                MuplarServices.queryInstalledPackages()) {
            result.add(createApplicationInfo(pkg));
        }
        return result;
    }
    @Override public List<PackageInfo> getInstalledPackages(int flags) {
        return Collections.emptyList();
    }
    @Override public String getInstallerPackageName(String packageName) { return null; }
    @Override public byte[] getInstantAppCookie() { return new byte[0]; }
    @Override public int getInstantAppCookieMaxBytes() { return 0; }
    @Override public InstrumentationInfo getInstrumentationInfo(ComponentName className, int flags)
            throws NameNotFoundException { throw new NameNotFoundException(); }
    @Override public Intent getLaunchIntentForPackage(String packageName) { return null; }
    @Override public Intent getLeanbackLaunchIntentForPackage(String packageName) { return null; }
    @Override public String getNameForUid(int uid) { return packageName; }
    @Override public int[] getPackageGids(String packageName) { return new int[0]; }
    @Override public int[] getPackageGids(String packageName, int flags) { return new int[0]; }
    @Override public PackageInfo getPackageInfo(VersionedPackage versionedPackage, int flags)
            throws NameNotFoundException { throw new NameNotFoundException(); }
    @Override public PackageInfo getPackageInfo(String packageName, int flags)
            throws NameNotFoundException {
        if (!this.packageName.equals(packageName)) throw new NameNotFoundException(packageName);
        PackageInfo info = new PackageInfo();
        info.packageName = packageName;
        info.applicationInfo = applicationInfo;
        return info;
    }
    @Override public PackageInstaller getPackageInstaller() { return null; }
    @Override public int getPackageUid(String packageName, int flags)
            throws NameNotFoundException { return applicationInfo.uid; }
    @Override public String[] getPackagesForUid(int uid) { return new String[] { packageName }; }
    @Override public List<PackageInfo> getPackagesHoldingPermissions(String[] permissions, int flags) {
        return Collections.emptyList();
    }
    @Override public PermissionGroupInfo getPermissionGroupInfo(String name, int flags)
            throws NameNotFoundException { throw new NameNotFoundException(); }
    @Override public PermissionInfo getPermissionInfo(String name, int flags)
            throws NameNotFoundException { throw new NameNotFoundException(); }
    @Override public int getPreferredActivities(List<IntentFilter> outFilters,
            List<ComponentName> outActivities, String packageName) { return 0; }
    @Override public List<PackageInfo> getPreferredPackages(int flags) {
        return Collections.emptyList();
    }
    @Override public ProviderInfo getProviderInfo(ComponentName component, int flags)
            throws NameNotFoundException { throw new NameNotFoundException(); }
    @Override public ActivityInfo getReceiverInfo(ComponentName component, int flags)
            throws NameNotFoundException { throw new NameNotFoundException(); }
    @Override public Resources getResourcesForActivity(ComponentName activity) { return resources; }
    @Override public Resources getResourcesForApplication(ApplicationInfo app) { return resources; }
    @Override public Resources getResourcesForApplication(String appPackageName)
            throws NameNotFoundException {
        if (packageName.equals(appPackageName)) return resources;
        throw new NameNotFoundException(appPackageName);
    }
    @Override public ServiceInfo getServiceInfo(ComponentName component, int flags)
            throws NameNotFoundException { throw new NameNotFoundException(); }
    @Override public List<SharedLibraryInfo> getSharedLibraries(int flags) {
        return Collections.emptyList();
    }
    @Override public FeatureInfo[] getSystemAvailableFeatures() { return new FeatureInfo[0]; }
    @Override public String[] getSystemSharedLibraryNames() { return new String[0]; }
    @Override public CharSequence getText(String packageName, int resId, ApplicationInfo appInfo) {
        try { return resources.getText(resId); } catch (Throwable ignored) { return null; }
    }
    @Override public Drawable getUserBadgedDrawableForDensity(Drawable drawable,
            UserHandle user, Rect badgeLocation, int badgeDensity) { return drawable; }
    @Override public Drawable getUserBadgedIcon(Drawable icon, UserHandle user) { return icon; }
    @Override public CharSequence getUserBadgedLabel(CharSequence label, UserHandle user) {
        return label;
    }
    @Override public XmlResourceParser getXml(String packageName, int resId,
            ApplicationInfo appInfo) {
        try { return resources.getXml(resId); } catch (Throwable ignored) { return null; }
    }
    @Override public boolean hasSystemFeature(String name) { return false; }
    @Override public boolean hasSystemFeature(String name, int version) { return false; }
    @Override public boolean isInstantApp() { return false; }
    @Override public boolean isInstantApp(String packageName) { return false; }
    @Override public boolean isPermissionRevokedByPolicy(String permName, String pkgName) {
        return false;
    }
    @Override public boolean isSafeMode() { return false; }
    @Override public List<ResolveInfo> queryBroadcastReceivers(Intent intent, int flags) {
        return Collections.emptyList();
    }
    @Override public List<ProviderInfo> queryContentProviders(String processName, int uid, int flags) {
        return Collections.emptyList();
    }
    @Override public List<InstrumentationInfo> queryInstrumentation(String targetPackage, int flags) {
        return Collections.emptyList();
    }
    @Override public List<ResolveInfo> queryIntentActivities(Intent intent, int flags) {
        java.util.ArrayList<ResolveInfo> result =
            new java.util.ArrayList<ResolveInfo>();
        for (MuplarServices.InstalledPackage pkg :
                MuplarServices.queryInstalledPackages()) {
            if (pkg.activity != null && !pkg.activity.isEmpty()) {
                result.add(createResolveInfo(pkg));
            }
        }
        if (result.isEmpty()) {
            result.add(createResolveInfo());
        }
        System.out.println("[Muplar/ART] PackageManager.queryIntentActivities count="
            + result.size());
        return result;
    }
    @Override public List<ResolveInfo> queryIntentActivityOptions(ComponentName caller,
            Intent[] specifics, Intent intent, int flags) { return Collections.emptyList(); }
    @Override public List<ResolveInfo> queryIntentContentProviders(Intent intent, int flags) {
        return Collections.emptyList();
    }
    @Override public List<ResolveInfo> queryIntentServices(Intent intent, int flags) {
        return Collections.emptyList();
    }
    @Override public List<PermissionInfo> queryPermissionsByGroup(String group, int flags) {
        return Collections.emptyList();
    }
    @Override public void removePackageFromPreferred(String packageName) {}
    @Override public void removePermission(String name) {}
    @Override public ResolveInfo resolveActivity(Intent intent, int flags) {
        return createResolveInfo();
    }
    @Override public ProviderInfo resolveContentProvider(String name, int flags) { return null; }
    @Override public ResolveInfo resolveService(Intent intent, int flags) { return null; }
    @Override public void setApplicationCategoryHint(String packageName, int categoryHint) {}
    @Override public void setApplicationEnabledSetting(String packageName, int newState, int flags) {}
    @Override public void setComponentEnabledSetting(ComponentName componentName, int newState, int flags) {}
    @Override public void setInstallerPackageName(String targetPackage, String installerPackageName) {}
    @Override public void updateInstantAppCookie(byte[] cookie) {}
    @Override public void verifyPendingInstall(int id, int verificationCode) {}

    private ResolveInfo createResolveInfo() {
        ActivityInfo activity = new ActivityInfo();
        activity.packageName = packageName;
        activity.name = packageName + ".Launcher";
        activity.applicationInfo = applicationInfo;
        activity.enabled = true;
        activity.exported = true;

        ResolveInfo info = new ResolveInfo();
        info.activityInfo = activity;
        return info;
    }

    private ResolveInfo createResolveInfo(MuplarServices.InstalledPackage pkg) {
        ResolveInfo info = new ResolveInfo();
        info.activityInfo = createActivityInfo(pkg);
        if (pkg.label != null && !pkg.label.isEmpty())
            info.nonLocalizedLabel = pkg.label;
        return info;
    }

    private ActivityInfo createActivityInfo(MuplarServices.InstalledPackage pkg) {
        ActivityInfo activity = new ActivityInfo();
        activity.packageName = pkg.packageName;
        activity.name = pkg.activity;
        activity.processName = pkg.packageName;
        activity.applicationInfo = createApplicationInfo(pkg);
        activity.enabled = true;
        activity.exported = true;
        if (pkg.label != null && !pkg.label.isEmpty())
            activity.nonLocalizedLabel = pkg.label;
        return activity;
    }

    private ApplicationInfo createApplicationInfo(
            MuplarServices.InstalledPackage pkg) {
        ApplicationInfo info = new ApplicationInfo();
        info.packageName = pkg.packageName;
        info.processName = pkg.packageName;
        info.sourceDir = pkg.apk;
        info.publicSourceDir = pkg.apk;
        info.uid = applicationInfo != null ? applicationInfo.uid : 10000;
        info.targetSdkVersion = 35;
        if (pkg.label != null && !pkg.label.isEmpty())
            info.nonLocalizedLabel = pkg.label;
        return info;
    }
}
