package android.content.pm;

import android.content.Intent;
import java.io.File;
import java.io.FileInputStream;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Properties;
import java.util.concurrent.CopyOnWriteArrayList;
import java.nio.file.FileSystems;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardWatchEventKinds;
import java.nio.file.WatchEvent;
import java.nio.file.WatchKey;
import java.nio.file.WatchService;

public class PackageManager {
    private volatile List<ApplicationInfo> applications;
    private final List<Runnable> packageChangeListeners =
        new CopyOnWriteArrayList<Runnable>();

    public static final int GET_META_DATA = 0x00000080;

    public PackageManager() {
        applications = loadApplications();
        startRegistryWatcher();
    }

    public List<ApplicationInfo> getInstalledApplications(int flags) {
        return Collections.unmodifiableList(applications);
    }

    public void registerPackageChangeListener(Runnable listener) {
        if (listener != null) packageChangeListeners.add(listener);
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

    private static List<ApplicationInfo> loadApplications() {
        List<ApplicationInfo> result = new ArrayList<ApplicationInfo>();
        String path = System.getProperty("muplar.package.registry", "");
        if (path.isEmpty()) return result;

        Properties registry = new Properties();
        File file = new File(path);
        if (!file.isFile()) return result;
        try (FileInputStream input = new FileInputStream(file)) {
            registry.load(input);
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
        } catch (Exception error) {
            System.err.println("[PackageManager] registry read failed: " + error);
        }
        return result;
    }

    private void startRegistryWatcher() {
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
                            applications = loadApplications();
                            for (Runnable listener : packageChangeListeners) {
                                try { listener.run(); }
                                catch (RuntimeException error) {
                                    System.err.println(
                                        "[PackageManager] listener failed: " +
                                        error);
                                }
                            }
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
