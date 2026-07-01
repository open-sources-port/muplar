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
    public void setArchiveCompatibility(ArchiveCompatibilityParams params) {}
    public List<PackageInstaller.SessionInfo> getAllPackageInstallerSessions() {
        return Collections.emptyList();
    }
    public void registerPackageInstallerSessionCallback(Executor executor,
            PackageInstaller.SessionCallback callback) {}
    public void unregisterPackageInstallerSessionCallback(
            PackageInstaller.SessionCallback callback) {}

    public List<LauncherActivityInfo> getActivityList(String packageName,
                                                       UserHandle user) {
        List<LauncherActivityInfo> result = new ArrayList<LauncherActivityInfo>();
        if (!new UserManager().isUserRunning(user)) return result;
        for (ApplicationInfo app : packageManager.getInstalledApplications(0)) {
            if (packageName == null || packageName.equals(app.packageName))
                result.add(new LauncherActivityInfo(app, packageManager, user));
        }
        return result;
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
