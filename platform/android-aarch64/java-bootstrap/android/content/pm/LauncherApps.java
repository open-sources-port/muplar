package android.content.pm;

import android.content.ComponentName;
import android.content.Intent;
import android.graphics.Rect;
import android.os.Bundle;
import android.os.UserHandle;
import android.os.UserManager;
import com.muplar.runtime.IntentDispatcher;
import java.util.ArrayList;
import java.util.List;
import java.util.HashSet;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.Executor;
import java.util.Collections;

public class LauncherApps {
    public static final class ShortcutQuery {
        private String packageName;
        private List<String> shortcutIds;
        private ComponentName activity;
        private int queryFlags;
        public ShortcutQuery setPackage(String value) {
            packageName = value; return this;
        }
        public ShortcutQuery setShortcutIds(List<String> value) {
            shortcutIds = value; return this;
        }
        public ShortcutQuery setActivity(ComponentName value) {
            activity = value; return this;
        }
        public ShortcutQuery setQueryFlags(int value) {
            queryFlags = value; return this;
        }
    }
    public static final class ArchiveCompatibilityParams {
        public static final ArchiveCompatibilityParams DEFAULT =
            new ArchiveCompatibilityParams();
        public ArchiveCompatibilityParams() {}
        public ArchiveCompatibilityParams(boolean includeArchivedApps) {}
        public void setEnableUnarchivalConfirmation(boolean enabled) {}
    }
    private final PackageManager packageManager;
    private final ConcurrentHashMap<Callback, Runnable> callbacks =
        new ConcurrentHashMap<Callback, Runnable>();

    public LauncherApps(PackageManager packageManager) {
        this.packageManager = packageManager;
    }
    public java.util.Map<String, LauncherActivityInfo> getActivityOverrides() {
        return java.util.Collections.emptyMap();
    }
    public void setArchiveCompatibility(ArchiveCompatibilityParams params) {}
    public List<PackageInstaller.SessionInfo> getAllPackageInstallerSessions() {
        return Collections.emptyList();
    }
    public List<ShortcutInfo> getShortcuts(ShortcutQuery query, UserHandle user) {
        return Collections.emptyList();
    }
    public boolean hasShortcutHostPermission() { return true; }
    public List<LauncherActivityInfo> getShortcutConfigActivityList(
            String packageName, UserHandle user) {
        return Collections.emptyList();
    }
    public boolean isPackageEnabled(String packageName, UserHandle user) {
        try {
            return packageManager.getApplicationInfo(packageName, 0).enabled;
        } catch (PackageManager.NameNotFoundException ignored) {
            return false;
        }
    }
    public ApplicationInfo getApplicationInfo(String packageName, int flags, UserHandle user)
            throws PackageManager.NameNotFoundException {
        if (!new UserManager().isUserRunning(user)) {
            throw new PackageManager.NameNotFoundException("User not running");
        }
        return packageManager.getApplicationInfo(packageName, flags);
    }
    public boolean isActivityEnabled(ComponentName component, UserHandle user) {
        if (!new UserManager().isUserRunning(user) || component == null) return false;
        try {
            packageManager.getActivityInfo(component, 0);
            return true;
        } catch (PackageManager.NameNotFoundException ignored) {
            return false;
        }
    }
    public void registerPackageInstallerSessionCallback(Executor executor,
            PackageInstaller.SessionCallback callback) {}
    public void unregisterPackageInstallerSessionCallback(
            PackageInstaller.SessionCallback callback) {}

    public List<LauncherActivityInfo> getActivityList(String packageName,
                                                       UserHandle user) {
        List<LauncherActivityInfo> result = new ArrayList<LauncherActivityInfo>();
        if (!new UserManager().isUserRunning(user)) {
            System.out.println("[LauncherApps] user is not running: " + user);
            return result;
        }
        for (ApplicationInfo app : packageManager.getInstalledApplications(0)) {
            if (app.launchActivity != null && !app.launchActivity.isEmpty() &&
                    (packageName == null || packageName.equals(app.packageName)))
                result.add(new LauncherActivityInfo(app, packageManager, user));
        }
        System.out.println("[LauncherApps] getActivityList package=" + packageName +
            " user=" + user + " count=" + result.size());
        return result;
    }

    public LauncherActivityInfo resolveActivity(Intent intent, UserHandle user) {
        if (intent == null || !new UserManager().isUserRunning(user)) return null;
        String packageName = intent.getComponent() == null
            ? intent.getPackage() : intent.getComponent().getPackageName();
        List<LauncherActivityInfo> matches = getActivityList(packageName, user);
        if (intent.getComponent() == null) return matches.isEmpty() ? null : matches.get(0);
        for (LauncherActivityInfo info : matches)
            if (intent.getComponent().equals(info.getComponentName())) return info;
        return null;
    }

    public void startMainActivity(ComponentName component, UserHandle user,
                                  Rect sourceBounds, Bundle options) {
        if (!new UserManager().isUserRunning(user))
            throw new SecurityException("unknown or stopped user");
        Intent intent = new Intent().setClassName(component.getPackageName(),
            component.getClassName());
        IntentDispatcher.launch(intent, packageManager);
    }

    public void registerCallback(final Callback callback) {
        if (callback == null || callbacks.containsKey(callback)) return;
        Runnable listener = new Runnable() {
            private Set<String> previous = packageNames();
            @Override public void run() {
                Set<String> current = packageNames();
                for (String name : current) {
                    if (!previous.contains(name))
                        callback.onPackageAdded(name, UserHandle.CURRENT);
                    else callback.onPackageChanged(name, UserHandle.CURRENT);
                }
                for (String name : previous) {
                    if (!current.contains(name))
                        callback.onPackageRemoved(name, UserHandle.CURRENT);
                }
                previous = current;
            }
            private Set<String> packageNames() {
                Set<String> names = new HashSet<String>();
                for (ApplicationInfo app :
                     packageManager.getInstalledApplications(0)) {
                    names.add(app.packageName);
                }
                return names;
            }
        };
        callbacks.put(callback, listener);
        packageManager.registerPackageChangeListener(listener);
    }

    public void unregisterCallback(Callback callback) {
        Runnable listener = callbacks.remove(callback);
        if (listener != null)
            packageManager.unregisterPackageChangeListener(listener);
    }

    public abstract static class Callback {
        public void onPackageRemoved(String packageName, UserHandle user) {}
        public void onPackageAdded(String packageName, UserHandle user) {}
        public void onPackageChanged(String packageName, UserHandle user) {}
        public void onPackagesAvailable(String[] packageNames, UserHandle user,
                                        boolean replacing) {}
        public void onPackagesUnavailable(String[] packageNames, UserHandle user,
                                          boolean replacing) {}
    }
}
