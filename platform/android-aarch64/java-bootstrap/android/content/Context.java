package android.content;

import android.content.pm.PackageManager;
import android.content.pm.ApplicationInfo;
import android.content.res.Resources;
import android.content.res.TypedArray;
import android.graphics.drawable.Drawable;
import android.util.AttributeSet;
import android.content.pm.LauncherApps;
import android.content.pm.ShortcutManager;
import android.appwidget.AppWidgetManager;
import android.app.ActivityManager;
import android.app.WallpaperManager;
import android.app.StatsManager;
import android.app.NotificationManager;
import android.app.KeyguardManager;
import android.app.admin.DevicePolicyManager;
import android.view.WindowManager;
import android.view.SimpleWindowManager;
import android.view.inputmethod.InputMethodManager;
import android.os.UserManager;
import android.os.Vibrator;
import android.hardware.display.DisplayManager;
import android.view.Display;
import android.os.Bundle;
import android.os.Handler;
import java.io.File;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.Executor;
import android.view.accessibility.AccessibilityManager;

public abstract class Context {
    private static volatile Context processApplication;
    public static final String LAUNCHER_APPS_SERVICE = "launcherapps";
    public static final String SHORTCUT_SERVICE = "shortcut";
    public static final String APPWIDGET_SERVICE = "appwidget";
    public static final String ACTIVITY_SERVICE = "activity";
    public static final String WINDOW_SERVICE = "window";
    public static final String INPUT_METHOD_SERVICE = "input_method";
    public static final String USER_SERVICE = "user";
    public static final String DISPLAY_SERVICE = "display";
    public static final String WALLPAPER_SERVICE = "wallpaper";
    public static final String STATS_MANAGER = "stats";
    public static final String NOTIFICATION_SERVICE = "notification";
    public static final String KEYGUARD_SERVICE = "keyguard";
    public static final String ACCESSIBILITY_SERVICE = "accessibility";
    public static final int MODE_PRIVATE = 0;
    public static final int BIND_AUTO_CREATE = 1;
    public static final int BIND_NOT_FOREGROUND = 4;
    public static final int BIND_WAIVE_PRIORITY = 32;
    private static final PackageManager pmInstance = new PackageManager();
    private static final Resources resourcesInstance = new Resources();
    private static final LauncherApps launcherAppsInstance =
        new LauncherApps(pmInstance);
    private static final ShortcutManager shortcutManagerInstance =
        new ShortcutManager();
    private static final ActivityManager activityManagerInstance =
        new ActivityManager();
    private static final WindowManager windowManagerInstance =
        new SimpleWindowManager();
    private static final InputMethodManager inputMethodManagerInstance =
        new InputMethodManager();
    private static final UserManager userManagerInstance = new UserManager();
    private static final Vibrator vibratorInstance = new Vibrator();
    private static final DisplayManager displayManagerInstance =
        new DisplayManager(windowManagerInstance.getDefaultDisplay());
    private static final StatsManager statsManagerInstance = new StatsManager();
    private static final NotificationManager notificationManagerInstance =
        new NotificationManager();
    private static final KeyguardManager keyguardManagerInstance =
        new KeyguardManager();
    private static final DevicePolicyManager devicePolicyManagerInstance =
        new DevicePolicyManager();
    private final ContentResolver contentResolver = new ContentResolver(this);
    private final Resources.Theme theme = resourcesInstance.newTheme();
    private final Map<BroadcastReceiver, IntentFilter> receivers =
        new ConcurrentHashMap<>();
    private final CopyOnWriteArrayList<ComponentCallbacks> componentCallbacks =
        new CopyOnWriteArrayList<>();
    private final Map<String, SharedPreferences> preferences =
        new ConcurrentHashMap<>();

    public PackageManager getPackageManager() {
        return pmInstance;
    }

    public ApplicationInfo getApplicationInfo() {
        try {
            return pmInstance.getApplicationInfo(getPackageName(), 0);
        } catch (PackageManager.NameNotFoundException ignored) {
            ApplicationInfo info = new ApplicationInfo();
            info.packageName = getPackageName();
            info.name = info.packageName;
            return info;
        }
    }

    protected static void setProcessApplication(Context application) {
        processApplication = application;
    }

    public Context getApplicationContext() {
        Context application = processApplication;
        return application == null ? this : application;
    }
    public Context createDeviceProtectedStorageContext() { return this; }
    public Context createCredentialProtectedStorageContext() { return this; }
    public boolean isDeviceProtectedStorage() { return false; }
    public boolean isCredentialProtectedStorage() { return true; }
    public Context createWindowContext(Display display, int type, Bundle options) {
        return this;
    }
    public Context createDisplayContext(Display display) { return this; }
    public Context createConfigurationContext(
            android.content.res.Configuration configuration) {
        return this;
    }
    public Display getDisplay() { return windowManagerInstance.getDefaultDisplay(); }

    public String getPackageName() {
        return System.getProperty("muplar.package.name", "");
    }

    public File getDataDir() { return appDirectory("data"); }
    public File getFilesDir() { return appDirectory("data/files"); }
    public File getFileStreamPath(String name) {
        return new File(getFilesDir(), name == null ? "" : name);
    }
    public File getCacheDir() { return appDirectory("data/cache"); }
    public File getCodeCacheDir() { return appDirectory("data/code_cache"); }
    public File getNoBackupFilesDir() { return appDirectory("data/no_backup"); }
    public File getDatabasePath(String name) {
        File directory = appDirectory("data/databases");
        return new File(directory, name == null ? "" : name);
    }
    public SharedPreferences getSharedPreferences(String name, int mode) {
        String safeName = (name == null ? "default" : name)
            .replaceAll("[^A-Za-z0-9._-]", "_");
        return preferences.computeIfAbsent(safeName, key ->
            new SimpleSharedPreferences(
                new File(appDirectory("data/shared_prefs"), key + ".properties")));
    }

    private File appDirectory(String relative) {
        File state = new File(System.getProperty("muplar.prefix.state.dir",
            System.getProperty("java.io.tmpdir")));
        String packageName = getPackageName().replaceAll("[^A-Za-z0-9._-]", "_");
        File directory = new File(new File(state, "apps"),
            (packageName.isEmpty() ? "unknown" : packageName) + "/" + relative);
        if (!directory.isDirectory() && !directory.mkdirs())
            throw new IllegalStateException("cannot create " + directory);
        return directory;
    }

    public Resources getResources() {
        return resourcesInstance;
    }

    public String getString(int id) {
        return resourcesInstance.getString(id);
    }
    public CharSequence getText(int id) { return resourcesInstance.getText(id); }
    public int getColor(int id) { return resourcesInstance.getColor(id); }
    public Drawable getDrawable(int id) { return resourcesInstance.getDrawable(id); }

    public Resources.Theme getTheme() { return theme; }
    public TypedArray obtainStyledAttributes(AttributeSet attributes, int[] styleable) {
        return new TypedArray(attributes, styleable, resourcesInstance.getDisplayMetrics());
    }
    public TypedArray obtainStyledAttributes(int[] styleable) {
        return obtainStyledAttributes(null, styleable);
    }
    public TypedArray obtainStyledAttributes(AttributeSet attributes, int[] styleable,
            int defStyleAttr, int defStyleRes) {
        return obtainStyledAttributes(attributes, styleable);
    }
    public TypedArray obtainStyledAttributes(int resourceId, int[] styleable) {
        return resourcesInstance.obtainStyledAttributes(resourceId, styleable);
    }

    public Object getSystemService(String name) {
        if ("launcherapps".equals(name)) return launcherAppsInstance;
        if ("shortcut".equals(name)) return shortcutManagerInstance;
        if ("appwidget".equals(name)) return AppWidgetManager.getInstance(this);
        if ("activity".equals(name)) return activityManagerInstance;
        if ("window".equals(name)) return windowManagerInstance;
        if ("input_method".equals(name)) return inputMethodManagerInstance;
        if ("user".equals(name)) return userManagerInstance;
        if ("display".equals(name)) return displayManagerInstance;
        if ("wallpaper".equals(name)) return WallpaperManager.getInstance(this);
        if ("stats".equals(name)) return statsManagerInstance;
        if ("notification".equals(name)) return notificationManagerInstance;
        if ("keyguard".equals(name)) return keyguardManagerInstance;
        if ("accessibility".equals(name)) return AccessibilityManager.getInstance(this);
        return null;
    }

    public <T> T getSystemService(Class<T> serviceClass) {
        if (serviceClass == LauncherApps.class) return serviceClass.cast(launcherAppsInstance);
        if (serviceClass == ShortcutManager.class) return serviceClass.cast(shortcutManagerInstance);
        if (serviceClass == AppWidgetManager.class)
            return serviceClass.cast(AppWidgetManager.getInstance(this));
        if (serviceClass == ActivityManager.class)
            return serviceClass.cast(activityManagerInstance);
        if (serviceClass == WindowManager.class)
            return serviceClass.cast(windowManagerInstance);
        if (serviceClass == InputMethodManager.class)
            return serviceClass.cast(inputMethodManagerInstance);
        if (serviceClass == AccessibilityManager.class)
            return serviceClass.cast(AccessibilityManager.getInstance(this));
        if (serviceClass == UserManager.class)
            return serviceClass.cast(userManagerInstance);
        if (serviceClass == Vibrator.class)
            return serviceClass.cast(vibratorInstance);
        if (serviceClass == DisplayManager.class)
            return serviceClass.cast(displayManagerInstance);
        if (serviceClass == WallpaperManager.class)
            return serviceClass.cast(WallpaperManager.getInstance(this));
        if (serviceClass == StatsManager.class)
            return serviceClass.cast(statsManagerInstance);
        if (serviceClass == NotificationManager.class)
            return serviceClass.cast(notificationManagerInstance);
        if (serviceClass == KeyguardManager.class)
            return serviceClass.cast(keyguardManagerInstance);
        if (serviceClass == DevicePolicyManager.class)
            return serviceClass.cast(devicePolicyManagerInstance);
        return null;
    }

    public ContentResolver getContentResolver() { return contentResolver; }
    public void setLocusContext(LocusId locusId, Bundle bundle) {}
    public int checkSelfPermission(String permission) {
        return pmInstance.checkPermission(permission,
            System.getProperty("muplar.package.name", ""));
    }
    public boolean bindService(Intent service, ServiceConnection connection,
            int flags) {
        if (connection != null)
            connection.onNullBinding(service == null ? null : service.getComponent());
        return false;
    }
    public void unbindService(ServiceConnection connection) {}
    public Intent registerReceiver(BroadcastReceiver receiver, IntentFilter filter) {
        if (receiver != null && filter != null) receivers.put(receiver, filter);
        return null;
    }
    public Intent registerReceiver(BroadcastReceiver receiver, IntentFilter filter,
            int flags) { return registerReceiver(receiver, filter); }
    public Intent registerReceiver(BroadcastReceiver receiver, IntentFilter filter,
            String broadcastPermission, Handler scheduler) {
        return registerReceiver(receiver, filter);
    }
    public Intent registerReceiver(BroadcastReceiver receiver, IntentFilter filter,
            String broadcastPermission, Handler scheduler, int flags) {
        return registerReceiver(receiver, filter);
    }
    public Executor getMainExecutor() {
        return new Executor() {
            public void execute(Runnable command) {
                if (command != null) command.run();
            }
        };
    }
    public void unregisterReceiver(BroadcastReceiver receiver) {
        receivers.remove(receiver);
    }
    public void sendBroadcast(Intent intent) {
        for (Map.Entry<BroadcastReceiver, IntentFilter> entry : receivers.entrySet())
            if (entry.getValue().hasAction(intent.getAction()))
                entry.getKey().onReceive(this, intent);
    }
    public void startActivity(Intent intent) {
        if (intent != null && (
                "com.android.launcher3".equals(intent.getComponentPackage()) ||
                (intent.getCategories() != null && intent.getCategories().contains(Intent.CATEGORY_HOME))
           )) {
            return;
        }
        if (intent != null && getPackageName().equals(intent.getComponentPackage()) &&
                (getPackageName() + ".proxy.ProxyActivityStarter").equals(
                    intent.getComponentClass())) {
            return;
        }
        com.muplar.runtime.IntentDispatcher.launch(intent, pmInstance);
    }
    public void startActivity(Intent intent, android.os.Bundle options) {
        startActivity(intent);
    }
    public void registerComponentCallbacks(ComponentCallbacks callback) {
        if (callback != null) componentCallbacks.addIfAbsent(callback);
    }
    public void unregisterComponentCallbacks(ComponentCallbacks callback) {
        componentCallbacks.remove(callback);
    }
}
