package com.muplar.runtime;

import android.content.Context;
import android.content.ContextWrapper;
import android.content.ContentResolver;
import android.content.AttributionSource;
import android.content.BroadcastReceiver;
import android.content.ComponentCallbacks;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.content.res.Configuration;
import android.content.res.Resources;
import android.database.DatabaseErrorHandler;
import android.database.sqlite.SQLiteDatabase;
import android.util.DisplayMetrics;
import android.os.Bundle;
import android.os.Binder;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.UserHandle;
import android.net.Uri;
import android.graphics.Rect;
import android.view.Display;
import android.view.WindowInsets;
import android.view.LayoutInflater;
import android.view.WindowManager;
import android.view.WindowMetrics;
import java.io.File;
import java.lang.reflect.Constructor;
import java.lang.reflect.Method;
import java.util.HashMap;
import java.util.Collections;
import java.util.Map;
import java.util.concurrent.Executor;

public final class MuplarContext extends ContextWrapper {
    private final String packageName;
    private final ClassLoader classLoader;
    private final ApplicationInfo applicationInfo;
    private final Resources resources;
    private final Resources.Theme theme;
    private final int themeResId;
    private final Object userManager;
    private final Object displayManager;
    private final Object windowManager;
    private final Object wallpaperManager;
    private final Object devicePolicyManager;
    private final Object vibrator;
    private final Object vibratorManager;
    private final Object sensorManager;
    private final LayoutInflater layoutInflater;
    private final Object launcherApps;
    private final Object statsManager;
    private final Object inputMethodManager;
    private final Object audioManager;
    private final Object appWidgetManager;
    private final Object activityManager;
    private final Object notificationManager;
    private final Object connectivityManager;
    private final Object powerManager;
    private final Object jobScheduler;
    private final Object accessibilityManager;
    private final Object autofillManager;
    private final Object clipboardManager;
    private final Object telephonyManager;
    private final Object wifiManager;
    private final ContentResolver contentResolver;
    private final PackageManager packageManager;
    private final IBinder activityToken = new Binder();
    private final Map<String, SharedPreferences> preferences = new HashMap<>();

    public MuplarContext(String packageName, String apkPath, ClassLoader classLoader) {
        super(null);
        this.packageName = packageName == null || packageName.isEmpty()
            ? "com.muplar.unknown" : packageName;
        this.classLoader = classLoader == null
            ? MuplarContext.class.getClassLoader() : classLoader;
        this.applicationInfo = new ApplicationInfo();
        this.applicationInfo.packageName = this.packageName;
        this.applicationInfo.processName = this.packageName;
        this.applicationInfo.sourceDir = apkPath;
        this.applicationInfo.publicSourceDir = apkPath;
        this.applicationInfo.dataDir = "/data/user/0/" + this.packageName;
        try {
            File dDir = new File(this.applicationInfo.dataDir);
            dDir.mkdirs();
            new File(dDir, "databases").mkdirs();
            new File(dDir, "shared_prefs").mkdirs();
            new File(dDir, "files").mkdirs();
            new File(dDir, "cache").mkdirs();
        } catch (Throwable ignored) {
        }
        this.applicationInfo.nativeLibraryDir = "/data/local/tmp/muplar/lib";
        this.applicationInfo.uid = 1000;
        this.applicationInfo.flags |= ApplicationInfo.FLAG_SYSTEM;
        this.applicationInfo.targetSdkVersion = 30;
        this.resources = createResources(apkPath);
        int launcherIcon = resources.getIdentifier("ic_launcher_home",
            "drawable", this.packageName);
        if (launcherIcon != 0) {
            this.applicationInfo.icon = launcherIcon;
        }
        this.themeResId = resolveThemeResource(resources, this.packageName);
        if (themeResId != 0) {
            this.applicationInfo.theme = themeResId;
        }
        this.theme = resources.newTheme();
        applyBaseAndAppTheme(this.theme, themeResId);
        this.userManager = createUserManager(this);
        this.displayManager = createDisplayManager(this);
        this.windowManager = createWindowManager(this);
        this.wallpaperManager = createWallpaperManager(this);
        this.devicePolicyManager = createDevicePolicyManager(this);
        this.vibratorManager = createVibratorManager(this);
        this.vibrator = createVibrator(this);
        this.sensorManager = createSensorManager();
        this.layoutInflater = new MuplarLayoutInflater(this);
        this.launcherApps = createLauncherApps(this);
        this.contentResolver = new MuplarContentResolver(this);
        this.packageManager =
            new MuplarPackageManager(this.packageName, applicationInfo, resources);
        this.statsManager = new android.app.StatsManager();
        this.inputMethodManager = createInputMethodManager(this);
        this.audioManager = createAudioManager(this);
        this.appWidgetManager = createAppWidgetManager(this);
        this.activityManager = createActivityManager(this);
        this.notificationManager = createNotificationManager(this);
        this.connectivityManager = createConnectivityManager(this);
        this.powerManager = createPowerManager(this);
        this.jobScheduler = createJobScheduler(this);
        this.accessibilityManager = createAccessibilityManager(this);
        this.autofillManager = createAutofillManager(this);
        this.clipboardManager = createClipboardManager(this);
        this.telephonyManager = createTelephonyManager(this);
        this.wifiManager = new android.net.wifi.WifiManager(this);
    }

    private Context applicationContext;

    public void setApplicationContext(Context applicationContext) {
        this.applicationContext = applicationContext;
    }

    @Override
    public Context getApplicationContext() {
        return applicationContext != null ? applicationContext : this;
    }

    @Override
    public Looper getMainLooper() {
        Looper looper = Looper.getMainLooper();
        return looper != null ? looper : Looper.myLooper();
    }

    public int getUserId() {
        return 0;
    }

    public UserHandle getUser() {
        try {
            return (UserHandle)Class.forName("android.os.UserHandle")
                .getMethod("of", Integer.TYPE)
                .invoke(null, Integer.valueOf(0));
        } catch (Throwable ignored) {
            try {
                java.lang.reflect.Constructor<?> ctor =
                    Class.forName("android.os.UserHandle")
                        .getDeclaredConstructor(Integer.TYPE);
                ctor.setAccessible(true);
                return (UserHandle)ctor.newInstance(Integer.valueOf(0));
            } catch (Throwable t) {
                throw new IllegalStateException("UserHandle unavailable", t);
            }
        }
    }

    @Override
    public ApplicationInfo getApplicationInfo() {
        return applicationInfo;
    }

    @Override
    public PackageManager getPackageManager() {
        return packageManager;
    }

    @Override
    public String getPackageName() {
        return packageName;
    }

    @Override
    public String getOpPackageName() {
        return packageName;
    }

    public String getBasePackageName() {
        return packageName;
    }

    public void setAutofillClient(android.view.autofill.AutofillManager.AutofillClient client) {
    }

    public android.view.autofill.AutofillManager.AutofillClient getAutofillClient() {
        return null;
    }

    public void setContentCaptureOptions(android.content.ContentCaptureOptions options) {
    }

    public android.content.ContentCaptureOptions getContentCaptureOptions() {
        return null;
    }

    public android.content.AutofillOptions getAutofillOptions() {
        return null;
    }

    public void setAutofillOptions(android.content.AutofillOptions options) {
    }

    public AttributionSource getAttributionSource() {
        try {
            return (AttributionSource)Class.forName("android.content.AttributionSource")
                .getConstructor(Integer.TYPE, String.class, String.class)
                .newInstance(Integer.valueOf(applicationInfo.uid), packageName, null);
        } catch (Throwable ignored) {
            try {
                java.lang.reflect.Constructor<?> ctor =
                    Class.forName("android.content.AttributionSource")
                        .getDeclaredConstructor();
                ctor.setAccessible(true);
                return (AttributionSource)ctor.newInstance();
            } catch (Throwable t) {
                throw new IllegalStateException("AttributionSource unavailable", t);
            }
        }
    }

    public String getAttributionTag() {
        return null;
    }

    @Override
    public ClassLoader getClassLoader() {
        return classLoader;
    }

    @Override
    public Resources getResources() {
        return resources;
    }

    @Override
    public Resources.Theme getTheme() {
        return theme;
    }

    @Override
    public void setTheme(int resid) {
        if (resid != 0) {
            theme.applyStyle(resid, true);
        }
    }

    @Override
    public AssetManager getAssets() {
        return resources.getAssets();
    }

    @Override
    public Context createDeviceProtectedStorageContext() {
        return this;
    }

    public int getDisplayId() {
        return 0;
    }

    public Display getDisplay() {
        Object displayService = getSystemService(Context.DISPLAY_SERVICE);
        if (displayService != null) {
            try {
                Display display = (Display) displayService.getClass()
                    .getMethod("getDisplay", Integer.TYPE)
                    .invoke(displayService, Integer.valueOf(0));
                if (display != null) {
                    return display;
                }
            } catch (Throwable ignored) {
            }
        }
        return createDisplay();
    }

    public Display getDisplayNoVerify() {
        return getDisplay();
    }

    @Override
    public boolean isUiContext() {
        return true;
    }

    @Override
    public boolean isRestricted() {
        return false;
    }

    public boolean canLoadUnsafeResources() {
        return true;
    }

    @Override
    public Context createDisplayContext(Display display) {
        return this;
    }

    public Context createWindowContext(Display display, int type, Bundle options) {
        return this;
    }

    @Override
    public Context createConfigurationContext(Configuration overrideConfiguration) {
        return this;
    }

    @Override
    public Context createPackageContext(String packageName, int flags)
        throws PackageManager.NameNotFoundException {
        return this;
    }

    @Override
    public int checkPermission(String permission, int pid, int uid) {
        return PackageManager.PERMISSION_GRANTED;
    }

    @Override
    public int checkCallingPermission(String permission) {
        return PackageManager.PERMISSION_GRANTED;
    }

    @Override
    public int checkCallingOrSelfPermission(String permission) {
        return PackageManager.PERMISSION_GRANTED;
    }

    @Override
    public int checkSelfPermission(String permission) {
        return PackageManager.PERMISSION_GRANTED;
    }

    @Override
    public boolean isDeviceProtectedStorage() {
        return false;
    }

    @Override
    public void startActivity(Intent intent) {
        startActivity(intent, null);
    }

    @Override
    public void startActivity(Intent intent, Bundle options) {
        if (intent != null) {
            String targetPkg = intent.getPackage();
            String targetCls = null;
            if (intent.getComponent() != null) {
                targetPkg = intent.getComponent().getPackageName();
                targetCls = intent.getComponent().getClassName();
            }
            if (targetPkg == null || targetPkg.isEmpty()) {
                targetPkg = this.packageName;
            }
            MuplarServices.InstalledPackage pkg = MuplarServices.findInstalledPackage(targetPkg);
            String apk = pkg != null ? pkg.apk : this.applicationInfo.sourceDir;
            String appCls = pkg != null ? pkg.application : null;
            if (targetCls == null && pkg != null) {
                targetCls = pkg.activity;
            }
            if (targetCls != null && !targetCls.isEmpty()) {
                System.out.println("[Muplar/ART] Context.startActivity targetPkg=" + targetPkg + " cls=" + targetCls);
                FrameworkDeviceController.launchApp(apk, targetPkg, targetCls, appCls);
            }
        }
    }

    @Override
    public void grantUriPermission(String toPackage, Uri uri, int modeFlags) {
    }

    @Override
    public void revokeUriPermission(Uri uri, int modeFlags) {
    }

    @Override
    public void revokeUriPermission(String toPackage, Uri uri, int modeFlags) {
    }

    @Override
    public int checkUriPermission(Uri uri, int pid, int uid, int modeFlags) {
        return PackageManager.PERMISSION_GRANTED;
    }

    @Override
    public int checkCallingUriPermission(Uri uri, int modeFlags) {
        return PackageManager.PERMISSION_GRANTED;
    }

    @Override
    public int checkCallingOrSelfUriPermission(Uri uri, int modeFlags) {
        return PackageManager.PERMISSION_GRANTED;
    }

    @Override
    public int checkUriPermission(Uri uri, String readPermission, String writePermission, int pid, int uid, int modeFlags) {
        return PackageManager.PERMISSION_GRANTED;
    }

    @Override
    public void registerComponentCallbacks(ComponentCallbacks callback) {
    }

    @Override
    public void unregisterComponentCallbacks(ComponentCallbacks callback) {
    }

    @Override
    public SharedPreferences getSharedPreferences(String name, int mode) {
        String key = name == null ? "" : name;
        SharedPreferences prefs = preferences.get(key);
        if (prefs == null) {
            prefs = new MuplarSharedPreferences();
            preferences.put(key, prefs);
        }
        return prefs;
    }

    @Override
    public File getDataDir() {
        File dir = new File(applicationInfo.dataDir);
        if (!dir.exists()) {
            dir.mkdirs();
        }
        return dir;
    }

    @Override
    public File getFilesDir() {
        File dir = new File(applicationInfo.dataDir, "files");
        if (!dir.exists()) {
            dir.mkdirs();
        }
        return dir;
    }

    @Override
    public File getNoBackupFilesDir() {
        File dir = new File(applicationInfo.dataDir, "no_backup");
        if (!dir.exists()) {
            dir.mkdirs();
        }
        return dir;
    }

    @Override
    public File getCacheDir() {
        File dir = new File(applicationInfo.dataDir, "cache");
        if (!dir.exists()) {
            dir.mkdirs();
        }
        return dir;
    }

    @Override
    public File getCodeCacheDir() {
        File dir = new File(applicationInfo.dataDir, "code_cache");
        if (!dir.exists()) {
            dir.mkdirs();
        }
        return dir;
    }

    @Override
    public File getDir(String name, int mode) {
        File dir = new File(applicationInfo.dataDir, "app_" + name);
        if (!dir.exists()) {
            dir.mkdirs();
        }
        return dir;
    }

    @Override
    public File getDatabasePath(String name) {
        if (name != null && (name.startsWith(File.separator) || name.contains(File.separator))) {
            File f = new File(name);
            File parent = f.getParentFile();
            if (parent != null && !parent.exists()) {
                parent.mkdirs();
            }
            return f;
        }
        File dir = new File(applicationInfo.dataDir, "databases");
        if (!dir.exists()) {
            dir.mkdirs();
        }
        return new File(dir, name == null ? "database.db" : name);
    }

    @Override
    public File getFileStreamPath(String name) {
        File dir = new File(applicationInfo.dataDir, "files");
        if (!dir.exists()) {
            dir.mkdirs();
        }
        return new File(dir, name == null ? "" : name);
    }

    @Override
    public SQLiteDatabase openOrCreateDatabase(String name,
                                               int mode,
                                               SQLiteDatabase.CursorFactory factory) {
        return SQLiteDatabase.openOrCreateDatabase(getDatabasePath(name), factory);
    }

    @Override
    public SQLiteDatabase openOrCreateDatabase(String name,
                                               int mode,
                                               SQLiteDatabase.CursorFactory factory,
                                               DatabaseErrorHandler errorHandler) {
        return SQLiteDatabase.openOrCreateDatabase(getDatabasePath(name), factory);
    }

    @Override
    public boolean deleteDatabase(String name) {
        return getDatabasePath(name).delete();
    }

    @Override
    public String[] databaseList() {
        File dir = new File(applicationInfo.dataDir, "databases");
        String[] names = dir.list();
        return names == null ? new String[0] : names;
    }

    @Override
    public Object getSystemService(String name) {
        if (Context.ACTIVITY_SERVICE.equals(name) || "activity".equals(name) || "android.app.ActivityManager".equals(name)) {
            return activityManager;
        }
        if (Context.USER_SERVICE.equals(name)) {
            return userManager;
        }
        if (Context.DISPLAY_SERVICE.equals(name)) {
            return displayManager;
        }
        if (Context.WINDOW_SERVICE.equals(name)) {
            return windowManager;
        }
        if (Context.WALLPAPER_SERVICE.equals(name)) {
            return wallpaperManager;
        }
        if (Context.DEVICE_POLICY_SERVICE.equals(name)) {
            return devicePolicyManager;
        }
        if (Context.VIBRATOR_SERVICE.equals(name)) {
            return vibrator;
        }
        if ("vibrator_manager".equals(name)) {
            return vibratorManager;
        }
        if (Context.SENSOR_SERVICE.equals(name)) {
            return sensorManager;
        }
        if (Context.LAYOUT_INFLATER_SERVICE.equals(name)) {
            return layoutInflater;
        }
        if (Context.INPUT_METHOD_SERVICE.equals(name) || "input_method".equals(name)) {
            return inputMethodManager;
        }
        if ("launcherapps".equals(name)) {
            return launcherApps;
        }
        if ("stats".equals(name)) {
            return statsManager;
        }
        if (Context.AUDIO_SERVICE.equals(name) || "audio".equals(name)) {
            return audioManager;
        }
        if ("appwidget".equals(name)) {
            return appWidgetManager;
        }
        if (Context.NOTIFICATION_SERVICE.equals(name) || "notification".equals(name) || "android.app.NotificationManager".equals(name)) {
            return notificationManager;
        }
        if (Context.CONNECTIVITY_SERVICE.equals(name) || "connectivity".equals(name) || "android.net.ConnectivityManager".equals(name)) {
            return connectivityManager;
        }
        if (Context.POWER_SERVICE.equals(name) || "power".equals(name) || "android.os.PowerManager".equals(name)) {
            return powerManager;
        }
        if (Context.JOB_SCHEDULER_SERVICE.equals(name) || "jobscheduler".equals(name) || "android.app.job.JobScheduler".equals(name)) {
            return jobScheduler;
        }
        if (Context.ACCESSIBILITY_SERVICE.equals(name) || "accessibility".equals(name) || "android.view.accessibility.AccessibilityManager".equals(name)) {
            return accessibilityManager;
        }
        if ("autofill".equals(name) || "android.view.autofill.AutofillManager".equals(name)) {
            return autofillManager;
        }
        if (Context.CLIPBOARD_SERVICE.equals(name) || "clipboard".equals(name) || "android.content.ClipboardManager".equals(name)) {
            return clipboardManager;
        }
        if (Context.TELEPHONY_SERVICE.equals(name) || "phone".equals(name) || "android.telephony.TelephonyManager".equals(name)) {
            return telephonyManager;
        }
        if (Context.WIFI_SERVICE.equals(name) || "wifi".equals(name) || "android.net.wifi.WifiManager".equals(name)) {
            return wifiManager;
        }
        return null;
    }

    @Override
    public Intent registerReceiver(BroadcastReceiver receiver, IntentFilter filter) {
        if (filter != null && filter.hasAction(Intent.ACTION_BATTERY_CHANGED)) {
            Intent intent = new Intent(Intent.ACTION_BATTERY_CHANGED);
            intent.putExtra(android.os.BatteryManager.EXTRA_STATUS, android.os.BatteryManager.BATTERY_STATUS_CHARGING);
            intent.putExtra(android.os.BatteryManager.EXTRA_PLUGGED, android.os.BatteryManager.BATTERY_PLUGGED_AC);
            intent.putExtra(android.os.BatteryManager.EXTRA_LEVEL, 100);
            intent.putExtra(android.os.BatteryManager.EXTRA_SCALE, 100);
            return intent;
        }
        return null;
    }

    @Override
    public Intent registerReceiver(BroadcastReceiver receiver,
                                   IntentFilter filter,
                                   int flags) {
        return registerReceiver(receiver, filter);
    }

    @Override
    public void unregisterReceiver(BroadcastReceiver receiver) {
    }

    @Override
    public Intent registerReceiver(BroadcastReceiver receiver,
                                   IntentFilter filter,
                                   String broadcastPermission,
                                   Handler scheduler) {
        return null;
    }

    @Override
    public Intent registerReceiver(BroadcastReceiver receiver,
                                   IntentFilter filter,
                                   String broadcastPermission,
                                   Handler scheduler,
                                   int flags) {
        return null;
    }

    @Override
    public boolean bindService(Intent service, android.content.ServiceConnection conn, int flags) {
        if (service != null && service.getComponent() != null &&
            "com.android.quickstep.TouchInteractionService".equals(service.getComponent().getClassName())) {
            return true;
        }
        return false;
    }

    @Override
    public void unbindService(android.content.ServiceConnection conn) {
    }

    @Override
    public boolean bindService(Intent service, int flags, Executor executor, android.content.ServiceConnection conn) {
        if (service != null && service.getComponent() != null &&
            "com.android.quickstep.TouchInteractionService".equals(service.getComponent().getClassName())) {
            return true;
        }
        return false;
    }

    @Override
    public android.content.ComponentName startService(Intent service) {
        return null;
    }

    @Override
    public android.content.ComponentName startForegroundService(Intent service) {
        return startService(service);
    }

    @Override
    public boolean stopService(Intent service) {
        return false;
    }

    @Override
    public Executor getMainExecutor() {
        final Handler handler = new Handler(getMainLooper());
        return new Executor() {
            @Override
            public void execute(Runnable command) {
                if (command != null) {
                    handler.post(command);
                }
            }
        };
    }

    public Handler getMainThreadHandler() {
        return new Handler(getMainLooper());
    }

    public IBinder getActivityToken() {
        return activityToken;
    }

    public IBinder getWindowContextToken() {
        return activityToken;
    }

    @Override
    public ContentResolver getContentResolver() {
        return contentResolver;
    }

    @Override
    public String getSystemServiceName(Class<?> serviceClass) {
        if (serviceClass != null &&
            "android.app.ActivityManager".equals(serviceClass.getName())) {
            return Context.ACTIVITY_SERVICE;
        }
        if (serviceClass != null &&
            "android.app.NotificationManager".equals(serviceClass.getName())) {
            return Context.NOTIFICATION_SERVICE;
        }
        if (serviceClass != null &&
            "android.net.ConnectivityManager".equals(serviceClass.getName())) {
            return Context.CONNECTIVITY_SERVICE;
        }
        if (serviceClass != null &&
            "android.os.PowerManager".equals(serviceClass.getName())) {
            return Context.POWER_SERVICE;
        }
        if (serviceClass != null &&
            ("android.app.job.JobScheduler".equals(serviceClass.getName()) || "android.app.JobSchedulerImpl".equals(serviceClass.getName()))) {
            return Context.JOB_SCHEDULER_SERVICE;
        }
        if (serviceClass != null &&
            "android.os.UserManager".equals(serviceClass.getName())) {
            return Context.USER_SERVICE;
        }
        if (serviceClass != null &&
            "android.hardware.display.DisplayManager".equals(serviceClass.getName())) {
            return Context.DISPLAY_SERVICE;
        }
        if (serviceClass != null &&
            "android.view.WindowManager".equals(serviceClass.getName())) {
            return Context.WINDOW_SERVICE;
        }
        if (serviceClass != null &&
            "android.app.WallpaperManager".equals(serviceClass.getName())) {
            return Context.WALLPAPER_SERVICE;
        }
        if (serviceClass != null &&
            "android.os.Vibrator".equals(serviceClass.getName())) {
            return Context.VIBRATOR_SERVICE;
        }
        if (serviceClass != null &&
            "android.os.VibratorManager".equals(serviceClass.getName())) {
            return "vibrator_manager";
        }
        if (serviceClass != null &&
            "android.hardware.SensorManager".equals(serviceClass.getName())) {
            return Context.SENSOR_SERVICE;
        }
        if (serviceClass != null &&
            "android.view.LayoutInflater".equals(serviceClass.getName())) {
            return Context.LAYOUT_INFLATER_SERVICE;
        }
        if (serviceClass != null &&
            "android.content.pm.LauncherApps".equals(serviceClass.getName())) {
            return "launcherapps";
        }
        if (serviceClass != null &&
            "android.app.StatsManager".equals(serviceClass.getName())) {
            return "stats";
        }
        if (serviceClass != null &&
            "android.view.inputmethod.InputMethodManager".equals(serviceClass.getName())) {
            return Context.INPUT_METHOD_SERVICE;
        }
        if (serviceClass != null &&
            "android.app.admin.DevicePolicyManager".equals(serviceClass.getName())) {
            return Context.DEVICE_POLICY_SERVICE;
        }
        if (serviceClass != null &&
            "android.media.AudioManager".equals(serviceClass.getName())) {
            return Context.AUDIO_SERVICE;
        }
        if (serviceClass != null &&
            "android.appwidget.AppWidgetManager".equals(serviceClass.getName())) {
            return "appwidget";
        }
        if (serviceClass != null &&
            "android.view.accessibility.AccessibilityManager".equals(serviceClass.getName())) {
            return Context.ACCESSIBILITY_SERVICE;
        }
        if (serviceClass != null &&
            "android.view.autofill.AutofillManager".equals(serviceClass.getName())) {
            return "autofill";
        }
        if (serviceClass != null &&
            "android.content.ClipboardManager".equals(serviceClass.getName())) {
            return Context.CLIPBOARD_SERVICE;
        }
        if (serviceClass != null &&
            "android.telephony.TelephonyManager".equals(serviceClass.getName())) {
            return Context.TELEPHONY_SERVICE;
        }
        if (serviceClass != null &&
            "android.net.wifi.WifiManager".equals(serviceClass.getName())) {
            return Context.WIFI_SERVICE;
        }
        return serviceClass == null ? null : serviceClass.getName();
    }

    private static Object createAppWidgetManager(Context context) {
        try {
            Class<?> type = Class.forName("android.appwidget.AppWidgetManager");
            Object manager = null;
            for (java.lang.reflect.Constructor<?> ctor : type.getDeclaredConstructors()) {
                try {
                    ctor.setAccessible(true);
                    Class<?>[] params = ctor.getParameterTypes();
                    Object[] args = new Object[params.length];
                    for (int i = 0; i < params.length; i++) {
                        if (Context.class.isAssignableFrom(params[i])) {
                            args[i] = context;
                        } else if (params[i] == Integer.TYPE) {
                            args[i] = Integer.valueOf(0);
                        } else if (params[i] == Boolean.TYPE) {
                            args[i] = Boolean.FALSE;
                        } else {
                            args[i] = null;
                        }
                    }
                    manager = ctor.newInstance(args);
                    break;
                } catch (Throwable ignored) {
                }
            }
            if (manager == null) {
                manager = allocateWithoutConstructor(type);
            }
            if (manager != null) {
                setFieldIfPresent(manager, "mContext", context);
            }
            return manager;
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] failed to create AppWidgetManager: " + t);
            return null;
        }
    }

    private static Object createAudioManager(Context context) {
        try {
            Class<?> type = Class.forName("android.media.AudioManager");
            Object am = null;
            try {
                java.lang.reflect.Constructor<?> ctor = type.getDeclaredConstructor(Context.class);
                ctor.setAccessible(true);
                am = ctor.newInstance(context);
            } catch (Throwable t1) {
                try {
                    java.lang.reflect.Constructor<?> ctor = type.getDeclaredConstructor();
                    ctor.setAccessible(true);
                    am = ctor.newInstance();
                } catch (Throwable t2) {
                    am = allocateWithoutConstructor(type);
                }
            }
            if (am != null) {
                setFieldIfPresent(am, "mContext", context);
                setFieldIfPresent(am, "mOriginalContext", context);
            }
            return am;
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] failed to create AudioManager: " + t);
            return null;
        }
    }

    private static Object createInputMethodManager(Context context) {
        try {
            Class<?> type = Class.forName("android.view.inputmethod.InputMethodManager");
            Object imm = null;
            try {
                java.lang.reflect.Constructor<?> ctor =
                    type.getDeclaredConstructor(Looper.class);
                ctor.setAccessible(true);
                imm = ctor.newInstance(Looper.getMainLooper());
            } catch (Throwable ignored) {
                try {
                    java.lang.reflect.Constructor<?> ctor =
                        type.getDeclaredConstructor();
                    ctor.setAccessible(true);
                    imm = ctor.newInstance();
                } catch (Throwable ignored2) {
                    imm = allocateWithoutConstructor(type);
                }
            }
            if (imm != null) {
                setFieldIfPresent(imm, "mContext", context);
                setFieldIfPresent(imm, "mLock", new Object());
                setFieldIfPresent(imm, "mMainLooper", Looper.getMainLooper());
                try {
                    Class<?> hClass = Class.forName("android.view.inputmethod.InputMethodManager$H");
                    Object hInstance = allocateWithoutConstructor(hClass);
                    setFieldIfPresent(hInstance, "mLooper", Looper.getMainLooper());
                    setFieldIfPresent(imm, "mH", hInstance);
                } catch (Throwable ignored) {
                    setFieldIfPresent(imm, "mH", new Object());
                }
                try {
                    Class<?> delegateClass =
                        Class.forName("android.view.inputmethod.InputMethodManager$DelegateImpl");
                    Object delegate = allocateWithoutConstructor(delegateClass);
                    setFieldIfPresent(delegate, "this$0", imm);
                    setFieldIfPresent(imm, "mDelegate", delegate);
                } catch (Throwable ignored) {
                }
            }
            return imm;
        } catch (Throwable ignored) {
            return null;
        }
    }

    private static Object createLauncherApps(Context context) {
        try {
            Class<?> type = Class.forName("android.content.pm.LauncherApps");
            Class<?> ilserviceClass = Class.forName("android.content.pm.ILauncherApps");
            Class<?> stubClass = Class.forName("android.content.pm.ILauncherApps$Stub");
            IBinder binder = MuplarServices.getBinder("launcherapps");
            java.lang.reflect.Method asInterface = stubClass.getMethod("asInterface", IBinder.class);
            Object service = asInterface.invoke(null, binder);

            Object launcherApps = null;
            try {
                java.lang.reflect.Constructor<?> ctor = type.getDeclaredConstructor(Context.class, ilserviceClass);
                ctor.setAccessible(true);
                launcherApps = ctor.newInstance(context, service);
                System.out.println("[Muplar/ART] LauncherApps created with Context,ILauncherApps ctor");
            } catch (Throwable t1) {
                try {
                    java.lang.reflect.Constructor<?> ctor = type.getDeclaredConstructor(Context.class);
                    ctor.setAccessible(true);
                    launcherApps = ctor.newInstance(context);
                    setFieldIfPresent(launcherApps, "mService", service);
                    System.out.println("[Muplar/ART] LauncherApps created with Context ctor");
                } catch (Throwable t2) {
                    launcherApps = allocateWithoutConstructor(type);
                    setFieldIfPresent(launcherApps, "mContext", context);
                    setFieldIfPresent(launcherApps, "mCallbacks", new java.util.ArrayList<Object>());
                    setFieldIfPresent(launcherApps, "mDelegates", new java.util.ArrayList<Object>());
                    setFieldIfPresent(launcherApps, "mService", service);
                    System.out.println("[Muplar/ART] LauncherApps allocated without constructor");
                }
            }
            return launcherApps;
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] failed to create LauncherApps: " + t);
            return null;
        }
    }

    private static Object createDevicePolicyManager(Context context) {
        try {
            ClassLoader loader = context != null && context.getClassLoader() != null
                ? context.getClassLoader()
                : MuplarContext.class.getClassLoader();
            if (loader == null) {
                loader = ClassLoader.getSystemClassLoader();
            }
            Class<?> type = null;
            try {
                type = Class.forName("android.app.admin.DevicePolicyManager", false, loader);
            } catch (Throwable t) {
                try {
                    type = Class.forName("android.app.admin.DevicePolicyManager");
                } catch (Throwable ignored) {}
            }
            if (type == null) return null;

            Class<?> serviceType = null;
            try {
                serviceType = Class.forName("android.app.admin.IDevicePolicyManager", false, loader);
            } catch (Throwable t) {
                try {
                    serviceType = Class.forName("android.app.admin.IDevicePolicyManager");
                } catch (Throwable ignored) {}
            }

            IBinder binder = MuplarServices.getBinder("device_policy");
            if (binder == null) {
                binder = new Binder();
            }
            final IBinder finalBinder = binder;

            Object service = null;
            if (binder != null) {
                try {
                    Class<?> stubClass = Class.forName("android.app.admin.IDevicePolicyManager$Stub", false, loader);
                    Method asInterface = stubClass.getMethod("asInterface", IBinder.class);
                    service = asInterface.invoke(null, finalBinder);
                } catch (Throwable ignored) {}
            }

            if (service == null && serviceType != null) {
                ClassLoader proxyLoader = loader != null ? loader : MuplarContext.class.getClassLoader();
                if (proxyLoader == null) {
                    proxyLoader = ClassLoader.getSystemClassLoader();
                }
                service = java.lang.reflect.Proxy.newProxyInstance(
                    proxyLoader,
                    new Class<?>[] { serviceType },
                    new java.lang.reflect.InvocationHandler() {
                        @Override
                        public Object invoke(Object proxy,
                                             java.lang.reflect.Method method,
                                             Object[] args) {
                            if ("asBinder".equals(method.getName())) {
                                return finalBinder;
                            }
                            if ("getManagedSubscriptionsPolicy".equals(method.getName())) {
                                return createManagedSubscriptionsPolicy();
                            }
                            return defaultValue(method.getReturnType());
                        }
                    });
            }

            Object manager = null;
            if (service != null && serviceType != null) {
                try {
                    Constructor<?> ctor = type.getDeclaredConstructor(Context.class, serviceType);
                    ctor.setAccessible(true);
                    manager = ctor.newInstance(context, service);
                    System.out.println("[Muplar/ART] DevicePolicyManager created with (Context, IDevicePolicyManager) ctor");
                } catch (Throwable ignored) {}
            }
            if (manager == null) {
                try {
                    Constructor<?> ctor = type.getDeclaredConstructor(Context.class, Handler.class);
                    ctor.setAccessible(true);
                    manager = ctor.newInstance(context, new Handler(Looper.getMainLooper()));
                    System.out.println("[Muplar/ART] DevicePolicyManager created with (Context, Handler) ctor");
                } catch (Throwable ignored) {}
            }
            if (manager == null) {
                try {
                    Constructor<?> ctor = type.getDeclaredConstructor(Context.class);
                    ctor.setAccessible(true);
                    manager = ctor.newInstance(context);
                    System.out.println("[Muplar/ART] DevicePolicyManager created with (Context) ctor");
                } catch (Throwable ignored) {}
            }
            if (manager == null) {
                manager = allocateWithoutConstructor(type);
                System.out.println("[Muplar/ART] DevicePolicyManager allocated without constructor");
            }

            if (manager != null) {
                setFieldIfPresent(manager, "mContext", context);
                setFieldIfPresent(manager, "mService", service);
                Object resMgr = null;
                try {
                    Method getResourcesMethod = type.getMethod("getResources");
                    resMgr = getResourcesMethod.invoke(manager);
                } catch (Throwable ignored) {}
                if (resMgr == null) {
                    resMgr = createDevicePolicyResourcesManager(context, service, loader);
                    setFieldIfPresent(manager, "mResourcesManager", resMgr);
                }
                System.out.println("[Muplar/ART] DevicePolicyManager ready: " + manager + " resMgr=" + resMgr);
                return manager;
            }
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] DevicePolicyManager create failed: " + t);
            t.printStackTrace(System.err);
            return null;
        }
        return null;
    }

    private static Object createDevicePolicyResourcesManager(Context context,
                                                            Object service,
                                                            ClassLoader loader) {
        try {
            Class<?> type = null;
            try {
                type = Class.forName("android.app.admin.DevicePolicyResourcesManager", false, loader);
            } catch (Throwable t) {
                try {
                    type = Class.forName("android.app.admin.DevicePolicyResourcesManager");
                } catch (Throwable ignored) {}
            }
            if (type == null) return null;

            Class<?> serviceType = null;
            try {
                serviceType = Class.forName("android.app.admin.IDevicePolicyManager", false, loader);
            } catch (Throwable ignored) {}

            Object resMgr = null;
            if (service != null && serviceType != null) {
                try {
                    Constructor<?> ctor = type.getDeclaredConstructor(Context.class, serviceType);
                    ctor.setAccessible(true);
                    resMgr = ctor.newInstance(context, service);
                    System.out.println("[Muplar/ART] DevicePolicyResourcesManager created with (Context, IDevicePolicyManager) ctor");
                } catch (Throwable ignored) {}
            }
            if (resMgr == null) {
                try {
                    Constructor<?> ctor = type.getDeclaredConstructor(Context.class);
                    ctor.setAccessible(true);
                    resMgr = ctor.newInstance(context);
                    System.out.println("[Muplar/ART] DevicePolicyResourcesManager created with (Context) ctor");
                } catch (Throwable ignored) {}
            }
            if (resMgr == null) {
                resMgr = allocateWithoutConstructor(type);
                System.out.println("[Muplar/ART] DevicePolicyResourcesManager allocated without constructor");
            }
            if (resMgr != null) {
                setFieldIfPresent(resMgr, "mContext", context);
                setFieldIfPresent(resMgr, "mService", service);
                return resMgr;
            }
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] DevicePolicyResourcesManager create failed: " + t);
        }
        return null;
    }

    private static Object createManagedSubscriptionsPolicy() {
        try {
            Class<?> type =
                Class.forName("android.app.admin.ManagedSubscriptionsPolicy");
            java.lang.reflect.Constructor<?> ctor =
                type.getDeclaredConstructor(Integer.TYPE);
            ctor.setAccessible(true);
            return ctor.newInstance(Integer.valueOf(0));
        } catch (Throwable ignored) {
            return null;
        }
    }

    private static Object createDisplayManager(Context context) {
        try {
            installDisplayManagerGlobal();
            Class<?> type = Class.forName("android.hardware.display.DisplayManager");
            java.lang.reflect.Constructor<?> ctor =
                type.getDeclaredConstructor(Context.class);
            ctor.setAccessible(true);
            return ctor.newInstance(context);
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] display manager create failed: "
                + t.getClass().getName() + ": " + t.getMessage());
            t.printStackTrace(System.err);
            return null;
        }
    }

    static Resources createResources(String apkPath) {
        try {
            java.lang.reflect.Constructor<AssetManager> ctor =
                AssetManager.class.getDeclaredConstructor();
            ctor.setAccessible(true);
            AssetManager assets = ctor.newInstance();
            java.lang.reflect.Method addAssetPath =
                AssetManager.class.getDeclaredMethod("addAssetPath", String.class);
            addAssetPath.setAccessible(true);
            File frameworkResources = new File("/system/framework/framework-res.apk");
            if (frameworkResources.isFile()) {
                addAssetPath.invoke(assets, frameworkResources.getAbsolutePath());
            }
            Object cookie = addAssetPath.invoke(assets, apkPath);
            if (cookie instanceof Integer && ((Integer) cookie).intValue() != 0) {
                return createResourcesFromAssets(assets);
            }
        } catch (Throwable ignored) {
        }

        try {
            Class<?> apkAssetsType =
                Class.forName("android.content.res.ApkAssets");
            java.lang.reflect.Method loadFromPath =
                apkAssetsType.getDeclaredMethod("loadFromPath", String.class);
            loadFromPath.setAccessible(true);
            Object apkAssets = loadFromPath.invoke(null, apkPath);

            Class<?> builderType =
                Class.forName("android.content.res.AssetManager$Builder");
            Object builder = builderType.getDeclaredConstructor().newInstance();
            java.lang.reflect.Method addApkAssets =
                builderType.getDeclaredMethod("addApkAssets", apkAssetsType);
            addApkAssets.setAccessible(true);
            File frameworkResources = new File("/system/framework/framework-res.apk");
            if (frameworkResources.isFile()) {
                Object frameworkAssets;
                try {
                    java.lang.reflect.Method loadSystemFromPath =
                        apkAssetsType.getDeclaredMethod(
                            "loadFromPath", String.class, Integer.TYPE);
                    loadSystemFromPath.setAccessible(true);
                    frameworkAssets = loadSystemFromPath.invoke(
                        null, frameworkResources.getAbsolutePath(), Integer.valueOf(1));
                } catch (NoSuchMethodException ignored) {
                    frameworkAssets =
                        loadFromPath.invoke(null, frameworkResources.getAbsolutePath());
                }
                addApkAssets.invoke(builder, frameworkAssets);
            }
            addApkAssets.invoke(builder, apkAssets);
            java.lang.reflect.Method build =
                builderType.getDeclaredMethod("build");
            build.setAccessible(true);
            AssetManager assets = (AssetManager) build.invoke(builder);
            return createResourcesFromAssets(assets);
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] apk resources create failed: "
                + t.getClass().getName() + ": " + t.getMessage());
            t.printStackTrace(System.err);
            return Resources.getSystem();
        }
    }

    private static Resources createResourcesFromAssets(AssetManager assets) {
        DisplayMetrics metrics = new DisplayMetrics();
        metrics.setToDefaults();
        metrics.widthPixels = 1080;
        metrics.heightPixels = 1920;
        metrics.densityDpi = DisplayMetrics.DENSITY_XXHIGH;
        metrics.density = metrics.densityDpi / 160.0f;
        metrics.scaledDensity = metrics.density;
        metrics.xdpi = metrics.densityDpi;
        metrics.ydpi = metrics.densityDpi;

        Configuration config = new Configuration();
        config.setToDefaults();
        config.orientation = Configuration.ORIENTATION_PORTRAIT;
        config.screenWidthDp = 360;
        config.screenHeightDp = 640;
        config.smallestScreenWidthDp = 360;
        config.densityDpi = metrics.densityDpi;
        return new Resources(assets, metrics, config);
    }

    public int getResolvedThemeResId() {
        return themeResId;
    }

    private static void applyBaseAndAppTheme(Resources.Theme theme, int appTheme) {
        applyFrameworkStyle(theme, "Theme_Material_Light");
        applyFrameworkStyle(theme, "Theme_DeviceDefault_Light_DarkActionBar");
        if (appTheme != 0) {
            try {
                theme.applyStyle(appTheme, true);
                System.out.println("[Muplar/ART] applied app theme 0x"
                    + Integer.toHexString(appTheme));
            } catch (Throwable t) {
                System.err.println("[Muplar/ART] app theme apply failed 0x"
                    + Integer.toHexString(appTheme) + ": " + t);
            }
        }
    }

    private static void applyFrameworkStyle(Resources.Theme theme, String name) {
        try {
            Class<?> styles = Class.forName("android.R$style");
            java.lang.reflect.Field field = styles.getField(name);
            theme.applyStyle(field.getInt(null), true);
        } catch (Throwable ignored) {
        }
    }

    private static int resolveThemeResource(Resources resources,
                                            String packageName) {
        String[] names = {
            "Theme_App",
            "Theme.App",
            "AppTheme",
            "LauncherTheme",
            "Theme_Launcher",
            "Theme_Launcher3",
            "LauncherThemeBase",
            "BaseLauncherTheme",
            "Theme_AppCompat",
            "Theme.AppCompat",
            "Theme_AppCompat_Light",
            "Theme.AppCompat.Light",
            "Theme_MaterialComponents",
            "Theme.MaterialComponents",
            "Theme_MaterialComponents_Light",
            "Theme.MaterialComponents.Light",
            "Theme",
        };
        for (String name : names) {
            try {
                int id = resources.getIdentifier(name, "style", packageName);
                if (id != 0) {
                    System.out.println("[Muplar/ART] resolved app theme "
                        + name + "=0x" + Integer.toHexString(id));
                    return id;
                }
            } catch (Throwable ignored) {
            }
        }
        return 0;
    }

    private static Object createWindowManager(final Context context) {
        try {
            Class<?> impl = Class.forName("android.view.WindowManagerImpl");
            for (java.lang.reflect.Constructor<?> ctor : impl.getDeclaredConstructors()) {
                ctor.setAccessible(true);
                Class<?>[] params = ctor.getParameterTypes();
                if (params.length == 1 && params[0].isAssignableFrom(context.getClass())) {
                    return ctor.newInstance(context);
                }
            }
            for (java.lang.reflect.Constructor<?> ctor : impl.getDeclaredConstructors()) {
                ctor.setAccessible(true);
                Class<?>[] params = ctor.getParameterTypes();
                Object[] args = new Object[params.length];
                for (int i = 0; i < params.length; i++) {
                    if (params[i].isAssignableFrom(context.getClass())) {
                        args[i] = context;
                    } else if (params[i] == Boolean.TYPE) {
                        args[i] = Boolean.FALSE;
                    } else if (params[i] == Integer.TYPE) {
                        args[i] = Integer.valueOf(0);
                    }
                }
                try {
                    return ctor.newInstance(args);
                } catch (Throwable ignored) {
                }
            }
            Object manager = allocateWithoutConstructor(impl);
            setFieldIfPresent(manager, "mContext", context);
            return manager;
        } catch (Throwable ignored) {
        }

        try {
            final Class<?> type = Class.forName("android.view.WindowManager");
            final WindowMetrics metrics = new WindowMetrics(
                new Rect(0, 0, 1080, 1920),
                WindowInsets.CONSUMED,
                3.0f);
            return java.lang.reflect.Proxy.newProxyInstance(
                type.getClassLoader(),
                new Class<?>[] { type },
                new java.lang.reflect.InvocationHandler() {
                    @Override
                    public Object invoke(Object proxy,
                                         java.lang.reflect.Method method,
                                         Object[] args) {
                        String name = method.getName();
                        if ("getDefaultDisplay".equals(name)) {
                            Object displayService =
                                context.getSystemService(Context.DISPLAY_SERVICE);
                            if (displayService != null) {
                                try {
                                    Object display = displayService.getClass()
                                        .getMethod("getDisplay", Integer.TYPE)
                                        .invoke(displayService, Integer.valueOf(0));
                                    if (display != null) {
                                        return display;
                                    }
                                } catch (Throwable ignored) {
                                }
                            }
                            return createDisplay();
                        }
                        if ("getCurrentWindowMetrics".equals(name) ||
                            "getMaximumWindowMetrics".equals(name)) {
                            return metrics;
                        }
                        if ("getPossibleMaximumWindowMetrics".equals(name)) {
                            return Collections.singleton(metrics);
                        }
                        return defaultValue(method.getReturnType());
                    }
                });
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] window manager create failed: "
                + t.getClass().getName() + ": " + t.getMessage());
            return null;
        }
    }

    private static Object createWallpaperManager(final Context context) {
        try {
            Class<?> managerType = Class.forName("android.app.WallpaperManager");
            Class<?> serviceType = Class.forName("android.app.IWallpaperManager");
            Object service = java.lang.reflect.Proxy.newProxyInstance(
                serviceType.getClassLoader(),
                new Class<?>[] { serviceType },
                new java.lang.reflect.InvocationHandler() {
                    @Override
                    public Object invoke(Object proxy,
                                         java.lang.reflect.Method method,
                                         Object[] args) {
                        String name = method.getName();
                        if ("getProfileIds".equals(name)
                            || "getProfileIdsWithDisabled".equals(name)) {
                            return new int[] { 0 };
                        }
                        if ("isUserUnlocked".equals(name)
                            || "isUserRunning".equals(name)
                            || "isWallpaperSupported".equals(name)
                            || "isSetWallpaperAllowed".equals(name)) {
                            return Boolean.TRUE;
                        }
                        if ("getUserSerialNumber".equals(name)) {
                            return Long.valueOf(0L);
                        }
                        if ("getUserHandle".equals(name)
                            || "getWallpaperId".equals(name)) {
                            return Integer.valueOf(1);
                        }
                        if ("getWallpaperColors".equals(name)
                            || "getWallpaperColorsWithFeature".equals(name)) {
                            return createSyntheticWallpaperColors();
                        }
                        return defaultValue(method.getReturnType());
                    }

                    private Object createSyntheticWallpaperColors() {
                        try {
                            Class<?> colorsCls = Class.forName("android.app.WallpaperColors");
                            Class<?> colorCls = Class.forName("android.graphics.Color");
                            java.lang.reflect.Method valueOf = colorCls.getMethod("valueOf", Integer.TYPE);
                            Object primaryColor = valueOf.invoke(null, Integer.valueOf(0xFF1E1E1E));
                            java.lang.reflect.Constructor<?> ctor = colorsCls.getConstructor(colorCls, colorCls, colorCls);
                            ctor.setAccessible(true);
                            return ctor.newInstance(primaryColor, null, null);
                        } catch (Throwable ignored) {
                            return null;
                        }
                    }
                });
            java.lang.reflect.Constructor<?> ctor =
                managerType.getDeclaredConstructor(
                    serviceType, Context.class, Handler.class);
            ctor.setAccessible(true);
            return ctor.newInstance(service, context, new Handler(Looper.getMainLooper()));
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] wallpaper manager create failed: "
                + t.getClass().getName() + ": " + t.getMessage());
            t.printStackTrace(System.err);
            return null;
        }
    }

    private static Object createVibrator(Context context) {
        try {
            Class<?> type = Class.forName("android.os.SystemVibrator");
            java.lang.reflect.Constructor<?> ctor =
                type.getDeclaredConstructor(Context.class);
            ctor.setAccessible(true);
            return ctor.newInstance(context);
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] SystemVibrator unavailable: "
                + t.getClass().getName() + ": " + t.getMessage());
            return null;
        }
    }

    private static Object createVibratorManager(Context context) {
        try {
            Class<?> type = Class.forName("android.os.SystemVibratorManager");
            java.lang.reflect.Constructor<?> ctor =
                type.getDeclaredConstructor(Context.class);
            ctor.setAccessible(true);
            return ctor.newInstance(context);
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] SystemVibratorManager unavailable: "
                + t.getClass().getName() + ": " + t.getMessage());
            return null;
        }
    }

    private static Object createSensorManager() {
        try {
            return Class.forName("android.hardware.MuplarSensorManager")
                .getDeclaredConstructor()
                .newInstance();
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] SensorManager unavailable: "
                + t.getClass().getName() + ": " + t.getMessage());
            return null;
        }
    }

    private static Display createDisplay() {
        try {
            Class<?> globalType =
                Class.forName("android.hardware.display.DisplayManagerGlobal");
            java.lang.reflect.Field instanceField =
                globalType.getDeclaredField("sInstance");
            instanceField.setAccessible(true);
            Object global = instanceField.get(null);
            if (global == null) {
                installDisplayManagerGlobal();
                global = instanceField.get(null);
            }
            Class<?> infoType = Class.forName("android.view.DisplayInfo");
            java.lang.reflect.Constructor<?> ctor =
                Display.class.getDeclaredConstructor(
                    globalType, Integer.TYPE, infoType, Resources.class);
            ctor.setAccessible(true);
            return (Display) ctor.newInstance(
                global, Integer.valueOf(0), createDisplayInfo(),
                Resources.getSystem());
        } catch (Throwable ignored) {
            return null;
        }
    }

    private static void installDisplayManagerGlobal() {
        try {
            Class<?> globalType =
                Class.forName("android.hardware.display.DisplayManagerGlobal");
            java.lang.reflect.Field instanceField =
                globalType.getDeclaredField("sInstance");
            instanceField.setAccessible(true);
            if (instanceField.get(null) != null) {
                System.out.println("[Muplar/ART] DisplayManagerGlobal already installed");
                return;
            }

            Class<?> managerInterface =
                Class.forName("android.hardware.display.IDisplayManager");
            Object service = java.lang.reflect.Proxy.newProxyInstance(
                managerInterface.getClassLoader(),
                new Class<?>[] { managerInterface },
                new java.lang.reflect.InvocationHandler() {
                    @Override
                    public Object invoke(Object proxy,
                                         java.lang.reflect.Method method,
                                         Object[] args) throws Throwable {
                        String name = method.getName();
                        if ("getDisplayInfo".equals(name)) {
                            int displayId = args != null && args.length > 0
                                ? ((Integer) args[0]).intValue() : 0;
                            return displayId == 0 ? createDisplayInfo() : null;
                        }
                        if ("getDisplayIds".equals(name)) {
                            return new int[] { 0 };
                        }
                        if ("getPreferredWideGamutColorSpaceId".equals(name)) {
                            return Integer.valueOf(0);
                        }
                        if ("getOverlaySupport".equals(name)) {
                            return null;
                        }
                        return defaultValue(method.getReturnType());
                    }
                });
            java.lang.reflect.Constructor<?> ctor =
                globalType.getDeclaredConstructor(managerInterface);
            ctor.setAccessible(true);
            instanceField.set(null, ctor.newInstance(service));
            System.out.println("[Muplar/ART] DisplayManagerGlobal installed");
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] DisplayManagerGlobal install failed: "
                + t.getClass().getName() + ": " + t.getMessage());
            t.printStackTrace(System.err);
        }
    }

    private static Object createDisplayInfo() throws Exception {
        Class<?> infoType = Class.forName("android.view.DisplayInfo");
        Object info = infoType.getDeclaredConstructor().newInstance();
        setFieldIfPresent(info, "displayId", Integer.valueOf(0));
        setFieldIfPresent(info, "displayGroupId", Integer.valueOf(0));
        setFieldIfPresent(info, "name", "Muplar Display");
        setFieldIfPresent(info, "uniqueId", "muplar:display:0");
        try {
            Class<?> addressType = Class.forName("android.view.DisplayAddress");
            java.lang.reflect.Method fromPhysicalDisplayId =
                addressType.getDeclaredMethod("fromPhysicalDisplayId", Long.TYPE);
            fromPhysicalDisplayId.setAccessible(true);
            setFieldIfPresent(info, "address",
                fromPhysicalDisplayId.invoke(null, Long.valueOf(1L)));
        } catch (Throwable ignored) {
        }
        setFieldIfPresent(info, "appWidth", Integer.valueOf(1080));
        setFieldIfPresent(info, "appHeight", Integer.valueOf(1920));
        setFieldIfPresent(info, "logicalWidth", Integer.valueOf(1080));
        setFieldIfPresent(info, "logicalHeight", Integer.valueOf(1920));
        setFieldIfPresent(info, "smallestNominalAppWidth", Integer.valueOf(1080));
        setFieldIfPresent(info, "smallestNominalAppHeight", Integer.valueOf(1920));
        setFieldIfPresent(info, "largestNominalAppWidth", Integer.valueOf(1080));
        setFieldIfPresent(info, "largestNominalAppHeight", Integer.valueOf(1920));
        setFieldIfPresent(info, "rotation", Integer.valueOf(0));
        setFieldIfPresent(info, "type", Integer.valueOf(1));
        setFieldIfPresent(info, "modeId", Integer.valueOf(1));
        setFieldIfPresent(info, "defaultModeId", Integer.valueOf(1));
        setFieldIfPresent(info, "renderFrameRate", Float.valueOf(60.0f));

        Class<?> modeType = Class.forName("android.view.Display$Mode");
        java.lang.reflect.Constructor<?> modeCtor =
            modeType.getDeclaredConstructor(
                Integer.TYPE, Integer.TYPE, Integer.TYPE, Float.TYPE);
        modeCtor.setAccessible(true);
        Object mode = modeCtor.newInstance(
            Integer.valueOf(1), Integer.valueOf(1080),
            Integer.valueOf(1920), Float.valueOf(60.0f));
        Object modes = java.lang.reflect.Array.newInstance(modeType, 1);
        java.lang.reflect.Array.set(modes, 0, mode);
        setFieldIfPresent(info, "supportedModes", modes);
        setFieldIfPresent(info, "supportedRefreshRates", new float[] { 60.0f });
        return info;
    }

    private static Object createUserManager(final Context context) {
        try {
            Class<?> type = Class.forName("android.os.UserManager");
            Class<?> serviceType = Class.forName("android.os.IUserManager");
            Object service = java.lang.reflect.Proxy.newProxyInstance(
                serviceType.getClassLoader(),
                new Class<?>[] { serviceType },
                new java.lang.reflect.InvocationHandler() {
                    @Override
                    public Object invoke(Object proxy,
                                         java.lang.reflect.Method method,
                                         Object[] args) {
                        String name = method.getName();
                        if ("getUserProfiles".equals(name)) {
                            java.util.ArrayList<UserHandle> profiles =
                                new java.util.ArrayList<UserHandle>();
                            profiles.add(((MuplarContext)context).getUser());
                            return profiles;
                        }
                        if ("getProfiles".equals(name)) {
                            java.util.ArrayList<Object> profiles =
                                new java.util.ArrayList<Object>();
                            Object info = buildUserInfo();
                            if (info != null) {
                                profiles.add(info);
                            }
                            return profiles;
                        }
                        if ("getProfileIds".equals(name) ||
                            "getProfileIdsWithDisabled".equals(name)) {
                            return new int[] { 0 };
                        }
                        if ("getSerialNumberForUser".equals(name)) {
                            return Long.valueOf(0L);
                        }
                        if ("getUserSerialNumber".equals(name)) {
                            return Integer.valueOf(0);
                        }
                        if ("getProfileType".equals(name)) {
                            return "android.os.usertype.full.SYSTEM";
                        }
                        if ("isUserRunning".equals(name) || "isUserUnlocked".equals(name)) {
                            return Boolean.TRUE;
                        }
                        return defaultValue(method.getReturnType());
                    }
                });
            try {
                java.lang.reflect.Constructor<?> ctor =
                    type.getDeclaredConstructor(Context.class, serviceType);
                ctor.setAccessible(true);
                Object constructed = ctor.newInstance(context, service);
                setFieldIfPresent(constructed, "mProfileTypeOfProcessUser", "android.os.usertype.full.SYSTEM");
                return constructed;
            } catch (Throwable t) {
                Object allocated = allocateWithoutConstructor(type);
                if (allocated != null) {
                    setFieldIfPresent(allocated, "mService", service);
                    setFieldIfPresent(allocated, "mContext", context);
                    setFieldIfPresent(allocated, "mProfileTypeOfProcessUser", "android.os.usertype.full.SYSTEM");
                    return allocated;
                }
            }
        } catch (Throwable ignored) {
        }
        return null;
    }

    private static Object buildUserInfo() {
        try {
            Class<?> type = Class.forName("android.content.pm.UserInfo");
            Object info = null;
            try {
                java.lang.reflect.Constructor<?> ctor =
                    type.getConstructor(Integer.TYPE, String.class, Integer.TYPE);
                info = ctor.newInstance(Integer.valueOf(0), "Owner",
                    Integer.valueOf(0));
            } catch (Throwable ignored) {
                try {
                    java.lang.reflect.Constructor<?> ctor =
                        type.getDeclaredConstructor();
                    ctor.setAccessible(true);
                    info = ctor.newInstance();
                } catch (Throwable ignored2) {
                    info = allocateWithoutConstructor(type);
                }
            }
            if (info != null) {
                setFieldIfPresent(info, "id", Integer.valueOf(0));
                setFieldIfPresent(info, "name", "Owner");
                setFieldIfPresent(info, "userType",
                    "android.os.usertype.full.SYSTEM");
                setFieldIfPresent(info, "profileGroupId", Integer.valueOf(0));
                setFieldIfPresent(info, "serialNumber", Integer.valueOf(0));
            }
            return info;
        } catch (Throwable ignored) {
            return null;
        }
    }

    private static void setFieldIfPresent(Object target, String name, Object value) {
        if (target == null || name == null) return;
        Class<?> cls = target.getClass();
        while (cls != null && cls != Object.class) {
            try {
                java.lang.reflect.Field field = cls.getDeclaredField(name);
                field.setAccessible(true);
                field.set(target, value);
                return;
            } catch (Throwable ignored) {
                cls = cls.getSuperclass();
            }
        }
    }

    private static Object defaultValue(Class<?> type) {
        if (type == Boolean.TYPE) return Boolean.TRUE;
        if (type == Byte.TYPE) return Byte.valueOf((byte)0);
        if (type == Short.TYPE) return Short.valueOf((short)0);
        if (type == Integer.TYPE) return Integer.valueOf(0);
        if (type == Long.TYPE) return Long.valueOf(0L);
        if (type == Float.TYPE) return Float.valueOf(0f);
        if (type == Double.TYPE) return Double.valueOf(0d);
        if (type == Character.TYPE) return Character.valueOf('\0');
        if (type != null && type.isArray()) {
            return java.lang.reflect.Array.newInstance(type.getComponentType(), 0);
        }
        if (type != null && Map.class.isAssignableFrom(type)) {
            return Collections.emptyMap();
        }
        return null;
    }

    private static Object createParceledListSlice() {
        try {
            Class<?> type = Class.forName("android.content.pm.ParceledListSlice");
            java.lang.reflect.Constructor<?> ctor =
                type.getConstructor(java.util.List.class);
            return ctor.newInstance(Collections.emptyList());
        } catch (Throwable ignored) {
            return null;
        }
    }

    private static Object allocateWithoutConstructor(Class<?> type) {
        try {
            Class<?> unsafeClass = Class.forName("sun.misc.Unsafe");
            java.lang.reflect.Field field =
                unsafeClass.getDeclaredField("theUnsafe");
            field.setAccessible(true);
            Object unsafe = field.get(null);
            java.lang.reflect.Method allocateInstance =
                unsafeClass.getMethod("allocateInstance", Class.class);
            return allocateInstance.invoke(unsafe, type);
        } catch (Throwable ignored) {
            return null;
        }
    }

    public android.app.IApplicationThread getIApplicationThread() {
        try {
            Class<?> threadClass = Class.forName("android.app.IApplicationThread", false, getClassLoader());
            return (android.app.IApplicationThread) java.lang.reflect.Proxy.newProxyInstance(
                getClassLoader(),
                new Class<?>[] { threadClass },
                new java.lang.reflect.InvocationHandler() {
                    @Override
                    public Object invoke(Object proxy, java.lang.reflect.Method method, Object[] args) throws Throwable {
                        if (method.getName().equals("asBinder")) {
                            return new android.os.Binder();
                        }
                        if (method.getReturnType().equals(Void.TYPE)) {
                            return null;
                        }
                        if (method.getReturnType().equals(Boolean.TYPE)) {
                            return Boolean.FALSE;
                        }
                        return null;
                    }
                }
            );
        } catch (Throwable t) {
            return null;
        }
    }

    private static Object createActivityManager(final Context context) {
        try {
            Class<?> type = Class.forName("android.app.ActivityManager");
            java.lang.reflect.Constructor<?>[] ctors = type.getDeclaredConstructors();
            System.out.println("[Muplar/ART] ActivityManager constructors count=" + ctors.length);
            for (java.lang.reflect.Constructor<?> ctor : ctors) {
                ctor.setAccessible(true);
                Class<?>[] params = ctor.getParameterTypes();
                System.out.println("[Muplar/ART] ActivityManager ctor params=" + java.util.Arrays.toString(params));
                if (params.length == 2) {
                    try {
                        android.os.Handler handler = null;
                        try {
                            if (android.os.Looper.getMainLooper() != null) {
                                handler = new android.os.Handler(android.os.Looper.getMainLooper());
                            }
                        } catch (Throwable ignored) {}
                        Object res = ctor.newInstance(context, handler);
                        System.out.println("[Muplar/ART] ActivityManager created successfully");
                        return res;
                    } catch (Throwable e) {
                        System.err.println("[Muplar/ART] ActivityManager(2) failed: " + e);
                    }
                } else if (params.length == 1) {
                    try {
                        Object res = ctor.newInstance(context);
                        System.out.println("[Muplar/ART] ActivityManager created(1)");
                        return res;
                    } catch (Throwable e) {
                        System.err.println("[Muplar/ART] ActivityManager(1) failed: " + e);
                    }
                } else if (params.length == 0) {
                    try {
                        Object res = ctor.newInstance();
                        System.out.println("[Muplar/ART] ActivityManager created(0)");
                        return res;
                    } catch (Throwable e) {
                        System.err.println("[Muplar/ART] ActivityManager(0) failed: " + e);
                    }
                }
            }
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] createActivityManager failed: " + t);
        }
        return null;
    }

    private static Object createNotificationManager(final Context context) {
        try {
            Class<?> type = Class.forName("android.app.NotificationManager");
            java.lang.reflect.Constructor<?>[] ctors = type.getDeclaredConstructors();
            System.out.println("[Muplar/ART] NotificationManager constructors count=" + ctors.length);
            for (java.lang.reflect.Constructor<?> ctor : ctors) {
                ctor.setAccessible(true);
                Class<?>[] params = ctor.getParameterTypes();
                System.out.println("[Muplar/ART] NotificationManager ctor params=" + java.util.Arrays.toString(params));
                if (params.length == 2) {
                    try {
                        Object res = ctor.newInstance(context, null);
                        System.out.println("[Muplar/ART] NotificationManager created(2)");
                        return res;
                    } catch (Throwable e) {
                        System.err.println("[Muplar/ART] NotificationManager(2) failed: " + e);
                    }
                } else if (params.length == 1) {
                    try {
                        Object res = ctor.newInstance(context);
                        System.out.println("[Muplar/ART] NotificationManager created(1)");
                        return res;
                    } catch (Throwable e) {
                        System.err.println("[Muplar/ART] NotificationManager(1) failed: " + e);
                    }
                } else if (params.length == 0) {
                    try {
                        Object res = ctor.newInstance();
                        System.out.println("[Muplar/ART] NotificationManager created(0)");
                        return res;
                    } catch (Throwable e) {
                        System.err.println("[Muplar/ART] NotificationManager(0) failed: " + e);
                    }
                }
            }
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] createNotificationManager failed: " + t);
        }
        return null;
    }

    private static Object createPowerManager(final Context context) {
        try {
            Class<?> type = Class.forName("android.os.PowerManager");
            IBinder binder = MuplarServices.getBinder("power");
            Object serviceProxy = null;
            try {
                Class<?> stubClass = Class.forName("android.os.IPowerManager$Stub");
                Method asInterface = stubClass.getMethod("asInterface", IBinder.class);
                serviceProxy = asInterface.invoke(null, binder);
            } catch (Throwable ignored) {}

            Constructor<?>[] ctors = type.getDeclaredConstructors();
            for (Constructor<?> ctor : ctors) {
                ctor.setAccessible(true);
                Class<?>[] params = ctor.getParameterTypes();
                if (params.length == 4 && params[0].isAssignableFrom(Context.class)) {
                    try {
                        Object res = ctor.newInstance(context, serviceProxy, null, new Handler(context.getMainLooper()));
                        return res;
                    } catch (Throwable ignored) {}
                } else if (params.length == 3 && params[0].isAssignableFrom(Context.class)) {
                    try {
                        Object res = ctor.newInstance(context, serviceProxy, new Handler(context.getMainLooper()));
                        return res;
                    } catch (Throwable ignored) {}
                } else if (params.length == 2 && params[0].isAssignableFrom(Context.class)) {
                    try {
                        Object res = ctor.newInstance(context, serviceProxy);
                        return res;
                    } catch (Throwable ignored) {}
                } else if (params.length == 1 && params[0].isAssignableFrom(Context.class)) {
                    try {
                        Object res = ctor.newInstance(context);
                        return res;
                    } catch (Throwable ignored) {}
                } else if (params.length == 0) {
                    try {
                        return ctor.newInstance();
                    } catch (Throwable ignored) {}
                }
            }
            Object allocated = allocateWithoutConstructor(type);
            if (allocated != null) {
                setFieldIfPresent(allocated, "mContext", context);
                if (serviceProxy != null) {
                    setFieldIfPresent(allocated, "mService", serviceProxy);
                }
                setFieldIfPresent(allocated, "mHandler", new Handler(context.getMainLooper()));
                return allocated;
            }
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] createPowerManager failed: " + t);
        }
        return null;
    }

    private static Object createConnectivityManager(final Context context) {
        try {
            return new android.net.ConnectivityManager(context);
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] createConnectivityManager failed: " + t);
            return null;
        }
    }

    private static Object createJobScheduler(final Context context) {
        try {
            return new android.app.job.MuplarJobScheduler(context);
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] createJobScheduler failed: " + t);
            return null;
        }
    }

    private static Object createAccessibilityManager(final Context context) {
        try {
            Class<?> type = Class.forName("android.view.accessibility.AccessibilityManager");
            IBinder binder = MuplarServices.getBinder("accessibility");
            Object serviceProxy = null;
            try {
                Class<?> stubClass = Class.forName("android.view.accessibility.IAccessibilityManager$Stub");
                Method asInterface = stubClass.getMethod("asInterface", IBinder.class);
                serviceProxy = asInterface.invoke(null, binder);
            } catch (Throwable ignored) {}

            Object instance = null;
            try {
                Method getInstance = type.getMethod("getInstance", Context.class);
                getInstance.setAccessible(true);
                instance = getInstance.invoke(null, context);
                if (instance != null) {
                    System.out.println("[Muplar/ART] AccessibilityManager obtained via getInstance");
                }
            } catch (Throwable ignored) {}

            if (instance == null) {
                Constructor<?>[] ctors = type.getDeclaredConstructors();
                for (Constructor<?> ctor : ctors) {
                    ctor.setAccessible(true);
                    Class<?>[] params = ctor.getParameterTypes();
                    if (params.length == 5) {
                        try {
                            instance = ctor.newInstance(context, new Handler(context.getMainLooper()), serviceProxy, 0, false);
                            break;
                        } catch (Throwable ignored) {}
                    } else if (params.length == 4) {
                        try {
                            instance = ctor.newInstance(context, new Handler(context.getMainLooper()), serviceProxy, 0);
                            break;
                        } catch (Throwable ignored) {}
                    } else if (params.length == 3) {
                        try {
                            instance = ctor.newInstance(context, serviceProxy, 0);
                            break;
                        } catch (Throwable ignored) {}
                    } else if (params.length == 2) {
                        try {
                            instance = ctor.newInstance(context, serviceProxy);
                            break;
                        } catch (Throwable ignored) {}
                    } else if (params.length == 1) {
                        try {
                            instance = ctor.newInstance(context);
                            break;
                        } catch (Throwable ignored) {}
                    } else if (params.length == 0) {
                        try {
                            instance = ctor.newInstance();
                            break;
                        } catch (Throwable ignored) {}
                    }
                }
            }

            if (instance == null) {
                instance = allocateWithoutConstructor(type);
                if (instance != null) {
                    setFieldIfPresent(instance, "mContext", context);
                    setFieldIfPresent(instance, "mHandler", new Handler(context.getMainLooper()));
                    if (serviceProxy != null) {
                        setFieldIfPresent(instance, "mService", serviceProxy);
                    }
                }
            }

            if (instance != null) {
                try {
                    java.lang.reflect.Field sInstanceField = type.getDeclaredField("sInstance");
                    sInstanceField.setAccessible(true);
                    sInstanceField.set(null, instance);
                } catch (Throwable ignored) {}
                System.out.println("[Muplar/ART] AccessibilityManager created successfully");
                return instance;
            }
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] createAccessibilityManager failed: " + t);
        }
        return null;
    }

    private static Object createAutofillManager(final Context context) {
        try {
            Class<?> type = Class.forName("android.view.autofill.AutofillManager");
            IBinder binder = MuplarServices.getBinder("autofill");
            Object serviceProxy = null;
            try {
                Class<?> stubClass = Class.forName("android.view.autofill.IAutoFillManager$Stub");
                Method asInterface = stubClass.getMethod("asInterface", IBinder.class);
                serviceProxy = asInterface.invoke(null, binder);
            } catch (Throwable ignored) {}

            Constructor<?>[] ctors = type.getDeclaredConstructors();
            for (Constructor<?> ctor : ctors) {
                ctor.setAccessible(true);
                Class<?>[] params = ctor.getParameterTypes();
                if (params.length == 2 && params[0].isAssignableFrom(Context.class)) {
                    try {
                        return ctor.newInstance(context, serviceProxy);
                    } catch (Throwable ignored) {}
                } else if (params.length == 1 && params[0].isAssignableFrom(Context.class)) {
                    try {
                        return ctor.newInstance(context);
                    } catch (Throwable ignored) {}
                } else if (params.length == 0) {
                    try {
                        return ctor.newInstance();
                    } catch (Throwable ignored) {}
                }
            }

            Object allocated = allocateWithoutConstructor(type);
            if (allocated != null) {
                setFieldIfPresent(allocated, "mContext", context);
                if (serviceProxy != null) {
                    setFieldIfPresent(allocated, "mService", serviceProxy);
                }
                return allocated;
            }
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] createAutofillManager failed: " + t);
        }
        return null;
    }

    private static Object createClipboardManager(final Context context) {
        try {
            Class<?> type = Class.forName("android.content.ClipboardManager");
            IBinder binder = MuplarServices.getBinder("clipboard");
            Object serviceProxy = null;
            try {
                Class<?> stubClass = Class.forName("android.content.IClipboard$Stub");
                Method asInterface = stubClass.getMethod("asInterface", IBinder.class);
                serviceProxy = asInterface.invoke(null, binder);
            } catch (Throwable ignored) {}

            Constructor<?>[] ctors = type.getDeclaredConstructors();
            for (Constructor<?> ctor : ctors) {
                ctor.setAccessible(true);
                Class<?>[] params = ctor.getParameterTypes();
                if (params.length == 2 && params[0].isAssignableFrom(Context.class)) {
                    try {
                        return ctor.newInstance(context, new Handler(context.getMainLooper()));
                    } catch (Throwable ignored) {}
                } else if (params.length == 1 && params[0].isAssignableFrom(Context.class)) {
                    try {
                        return ctor.newInstance(context);
                    } catch (Throwable ignored) {}
                } else if (params.length == 0) {
                    try {
                        return ctor.newInstance();
                    } catch (Throwable ignored) {}
                }
            }

            Object allocated = allocateWithoutConstructor(type);
            if (allocated != null) {
                setFieldIfPresent(allocated, "mContext", context);
                setFieldIfPresent(allocated, "mHandler", new Handler(context.getMainLooper()));
                if (serviceProxy != null) {
                    setFieldIfPresent(allocated, "mService", serviceProxy);
                }
                return allocated;
            }
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] createClipboardManager failed: " + t);
        }
        return null;
    }

    private static Object createTelephonyManager(final Context context) {
        try {
            Class<?> type = Class.forName("android.telephony.TelephonyManager");
            IBinder binder = MuplarServices.getBinder("phone");
            Object serviceProxy = null;
            try {
                Class<?> stubClass = Class.forName("com.android.internal.telephony.ITelephony$Stub");
                Method asInterface = stubClass.getMethod("asInterface", IBinder.class);
                serviceProxy = asInterface.invoke(null, binder);
            } catch (Throwable ignored) {}

            Constructor<?>[] ctors = type.getDeclaredConstructors();
            for (Constructor<?> ctor : ctors) {
                ctor.setAccessible(true);
                Class<?>[] params = ctor.getParameterTypes();
                if (params.length == 2 && params[0].isAssignableFrom(Context.class) && params[1] == int.class) {
                    try {
                        return ctor.newInstance(context, 0);
                    } catch (Throwable ignored) {}
                } else if (params.length == 1 && params[0].isAssignableFrom(Context.class)) {
                    try {
                        return ctor.newInstance(context);
                    } catch (Throwable ignored) {}
                } else if (params.length == 0) {
                    try {
                        return ctor.newInstance();
                    } catch (Throwable ignored) {}
                }
            }

            Object allocated = allocateWithoutConstructor(type);
            if (allocated != null) {
                setFieldIfPresent(allocated, "mContext", context);
                return allocated;
            }
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] createTelephonyManager failed: " + t);
        }
        return null;
    }
}
