package android.content.pm;

import android.content.Intent;
import android.content.ComponentName;
import android.graphics.drawable.Drawable;
import java.io.File;
import java.io.FileInputStream;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Properties;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.atomic.AtomicBoolean;
import java.nio.file.FileSystems;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardWatchEventKinds;
import java.nio.file.WatchEvent;
import java.nio.file.WatchKey;
import java.nio.file.WatchService;
import com.muplar.runtime.FrameworkServiceClient;

public class PackageManager {
    private final PackageInstaller packageInstaller = new PackageInstaller();
    private volatile List<ApplicationInfo> applications;
    private final List<Runnable> packageChangeListeners =
        new CopyOnWriteArrayList<Runnable>();
    private final AtomicBoolean fileWatcherStarted = new AtomicBoolean(false);
    private volatile Process serviceSubscription;

    public static final int GET_META_DATA = 0x00000080;
    public static final int PERMISSION_GRANTED = 0;
    public static final int PERMISSION_DENIED = -1;
    public static final int COMPONENT_ENABLED_STATE_DISABLED = 2;

    public PackageManager() {
        applications = loadApplications();
        if (!startServiceWatcher()) startFileRegistryWatcher();
    }
    public boolean isSafeMode() { return false; }
    public boolean hasSystemFeature(String feature) { return false; }
    public PackageInstaller getPackageInstaller() { return packageInstaller; }
    public void setApplicationEnabledSetting(String packageName, int newState,
            int flags) {}

    public List<ApplicationInfo> getInstalledApplications(int flags) {
        return Collections.unmodifiableList(applications);
    }

    public void registerPackageChangeListener(Runnable listener) {
        if (listener != null) packageChangeListeners.add(listener);
    }

    public void unregisterPackageChangeListener(Runnable listener) {
        if (listener != null) packageChangeListeners.remove(listener);
    }

    public Drawable getApplicationIcon(ApplicationInfo info) {
        if (info == null || info.iconPath == null || info.iconPath.isEmpty())
            return null;
        return new Drawable(info.iconPath);
    }

    public Drawable getApplicationIcon(String packageName)
            throws NameNotFoundException {
        return getApplicationIcon(getApplicationInfo(packageName, 0));
    }

    public Intent getLaunchIntentForPackage(String packageName) {
        if (packageName == null) return null;
        for (ApplicationInfo app : applications) {
            if (packageName.equals(app.packageName) &&
                app.launchActivity != null && !app.launchActivity.isEmpty()) {
                return new Intent().setClassName(app.packageName,
                    app.launchActivity);
            }
        }
        return null;
    }
    public ActivityInfo getActivityInfo(ComponentName component, int flags)
            throws NameNotFoundException {
        if (component == null) throw new NameNotFoundException("null component");
        for (ApplicationInfo app : applications) {
            if (component.getPackageName().equals(app.packageName) &&
                    component.getClassName().equals(app.launchActivity)) {
                ActivityInfo info = new ActivityInfo();
                info.applicationInfo = app;
                info.packageName = app.packageName;
                info.name = app.launchActivity;
                return info;
            }
        }
        if (component.getPackageName().equals(
                System.getProperty("muplar.package.name", ""))) {
            ActivityInfo info = new ActivityInfo();
            info.applicationInfo = getApplicationInfo(component.getPackageName(), flags);
            info.packageName = component.getPackageName();
            info.name = component.getClassName();
            return info;
        }
        throw new NameNotFoundException(component.toString());
    }
    public List<ResolveInfo> queryBroadcastReceivers(Intent intent, int flags) {
        return Collections.emptyList();
    }
    public List<ResolveInfo> queryIntentActivities(Intent intent, int flags) {
        List<ResolveInfo> result = new ArrayList<ResolveInfo>();
        for (ApplicationInfo app : applications) {
            ResolveInfo resolve = new ResolveInfo();
            resolve.activityInfo = new ActivityInfo();
            resolve.activityInfo.applicationInfo = app;
            resolve.activityInfo.packageName = app.packageName;
            resolve.activityInfo.name = app.launchActivity;
            result.add(resolve);
        }
        return result;
    }
    public ResolveInfo resolveActivity(Intent intent, int flags) {
        List<ResolveInfo> results = queryIntentActivities(intent, flags);
        return results.isEmpty() ? null : results.get(0);
    }

    public int checkPermission(String permissionName, String packageName) {
        if (permissionName == null || packageName == null)
            return PERMISSION_DENIED;
        String user = System.getProperty("muplar.user.id", "0");
        String result = FrameworkServiceClient.request("check-permission",
            packageName + "\n" + user + "\n" + permissionName);
        return "0".equals(result) ? PERMISSION_GRANTED : PERMISSION_DENIED;
    }

    public ApplicationInfo getApplicationInfo(String packageName, int flags)
            throws NameNotFoundException {
        if (packageName != null) {
            for (ApplicationInfo app : applications) {
                if (packageName.equals(app.packageName)) return app;
            }
        }
        throw new NameNotFoundException(packageName);
    }

    public static class NameNotFoundException extends Exception {
        public NameNotFoundException(String packageName) {
            super("package not found: " + packageName);
        }
    }

    private List<ApplicationInfo> loadApplications() {
        List<ApplicationInfo> result = new ArrayList<ApplicationInfo>();
        Properties registry = loadRegistryProperties();
        try {
            int count = Integer.parseInt(registry.getProperty("count", "0"));
            for (int i = 0; i < count; ++i) {
                String key = "package." + i + ".";
                String packageName = registry.getProperty(key + "name", "");
                String activity = registry.getProperty(key + "activity", "");
                if (packageName.isEmpty() || activity.isEmpty()) continue;
                ApplicationInfo app = new ApplicationInfo();
                app.packageName = packageName;
                app.name = registry.getProperty(key + "label", packageName);
                app.sourceDir = registry.getProperty(key + "apk", "");
                app.launchActivity = activity;
                app.iconPath = registry.getProperty(key + "icon", "");
                result.add(app);
            }
        } catch (RuntimeException error) {
            System.err.println("[PackageManager] registry read failed: " + error);
        }
        return result;
    }

    private Properties loadRegistryProperties() {
        Properties registry = new Properties();
        String executable = System.getProperty("muplar.service.executable", "");
        String socket = System.getProperty("muplar.service.socket", "");
        if (!executable.isEmpty() && !socket.isEmpty() &&
            new File(executable).isFile()) {
            try {
                Process process = new ProcessBuilder(executable, "--socket", socket,
                    "--client", "query-packages")
                    .redirectError(ProcessBuilder.Redirect.INHERIT).start();
                try (java.io.InputStream input = process.getInputStream()) {
                    registry.load(input);
                }
                if (process.waitFor() == 0) return registry;
                registry.clear();
            } catch (Exception error) {
                System.err.println("[PackageManager] metadata RPC failed: " + error);
                registry.clear();
            }
        }

        String path = System.getProperty("muplar.package.registry", "");
        File file = new File(path);
        if (!file.isFile()) return registry;
        try (FileInputStream input = new FileInputStream(file)) {
            registry.load(input);
        } catch (Exception error) {
            System.err.println("[PackageManager] registry fallback failed: " + error);
        }
        return registry;
    }

    private boolean startServiceWatcher() {
        final String executable =
            System.getProperty("muplar.service.executable", "");
        final String socket = System.getProperty("muplar.service.socket", "");
        if (executable.isEmpty() || socket.isEmpty() ||
            !new File(executable).isFile()) return false;
        try {
            serviceSubscription = new ProcessBuilder(executable,
                "--socket", socket, "--client", "subscribe-packages")
                .redirectError(ProcessBuilder.Redirect.INHERIT).start();
        } catch (Exception error) {
            System.err.println("[PackageManager] service subscription failed: " +
                error);
            return false;
        }
        Runtime.getRuntime().addShutdownHook(new Thread(new Runnable() {
            @Override public void run() {
                Process process = serviceSubscription;
                if (process != null) process.destroy();
            }
        }, "muplar-package-service-shutdown"));
        Thread watcher = new Thread(new Runnable() {
            @Override public void run() {
                try (BufferedReader input = new BufferedReader(
                         new InputStreamReader(serviceSubscription.getInputStream(),
                                               "UTF-8"))) {
                    String generation = input.readLine();
                    if (generation == null) {
                        startFileRegistryWatcher();
                        return;
                    }
                    while ((generation = input.readLine()) != null) {
                        notifyPackagesChanged();
                    }
                    startFileRegistryWatcher();
                } catch (Exception error) {
                    System.err.println("[PackageManager] service watch failed: " +
                        error);
                    startFileRegistryWatcher();
                }
            }
        }, "muplar-package-service");
        watcher.setDaemon(true);
        watcher.start();
        return true;
    }

    private void notifyPackagesChanged() {
        applications = loadApplications();
        for (Runnable listener : packageChangeListeners) {
            try { listener.run(); }
            catch (RuntimeException error) {
                System.err.println("[PackageManager] listener failed: " + error);
            }
        }
    }

    private void startFileRegistryWatcher() {
        if (!fileWatcherStarted.compareAndSet(false, true)) return;
        final String registryPath =
            System.getProperty("muplar.package.registry", "");
        if (registryPath.isEmpty()) return;
        Thread watcher = new Thread(new Runnable() {
            @Override public void run() {
                Path file = new File(registryPath).toPath().toAbsolutePath();
                Path parent = file.getParent();
                if (parent == null || !Files.isDirectory(parent)) return;
                try (WatchService service =
                         FileSystems.getDefault().newWatchService()) {
                    parent.register(service,
                        StandardWatchEventKinds.ENTRY_CREATE,
                        StandardWatchEventKinds.ENTRY_MODIFY,
                        StandardWatchEventKinds.ENTRY_DELETE);
                    while (true) {
                        WatchKey key = service.take();
                        boolean changed = false;
                        for (WatchEvent<?> event : key.pollEvents()) {
                            Object context = event.context();
                            if (context != null &&
                                file.getFileName().toString().equals(
                                    context.toString())) {
                                changed = true;
                            }
                        }
                        if (!key.reset()) return;
                        if (changed) {
                            notifyPackagesChanged();
                        }
                    }
                } catch (Exception error) {
                    System.err.println(
                        "[PackageManager] registry watch failed: " + error);
                }
            }
        }, "muplar-package-registry");
        watcher.setDaemon(true);
        watcher.start();
    }
}
