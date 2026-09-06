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
import android.graphics.Rect;
import android.view.Display;
import android.view.WindowInsets;
import android.view.LayoutInflater;
import android.view.WindowManager;
import android.view.WindowMetrics;
import java.io.File;
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
        this.applicationInfo.sourceDir = apkPath;
        this.applicationInfo.publicSourceDir = apkPath;
        this.applicationInfo.dataDir = "/data/user/0/" + this.packageName;
        this.applicationInfo.nativeLibraryDir = "/data/local/tmp/muplar/lib";
        this.applicationInfo.uid = 1000;
        this.applicationInfo.flags |= ApplicationInfo.FLAG_SYSTEM;
        this.applicationInfo.targetSdkVersion = 30;
        this.resources = createResources(apkPath);
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
        return null;
    }

    @Override
    public Intent registerReceiver(BroadcastReceiver receiver, IntentFilter filter) {
        return null;
    }

    @Override
    public Intent registerReceiver(BroadcastReceiver receiver,
                                   IntentFilter filter,
                                   int flags) {
        return null;
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
        return serviceClass == null ? null : serviceClass.getName();
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
            } catch (Throwable t1) {
                try {
                    java.lang.reflect.Constructor<?> ctor = type.getDeclaredConstructor(Context.class);
                    ctor.setAccessible(true);
                    launcherApps = ctor.newInstance(context);
                    setFieldIfPresent(launcherApps, "mService", service);
                } catch (Throwable t2) {
                    launcherApps = allocateWithoutConstructor(type);
                    setFieldIfPresent(launcherApps, "mContext", context);
                    setFieldIfPresent(launcherApps, "mCallbacks", new java.util.ArrayList<Object>());
                    setFieldIfPresent(launcherApps, "mDelegates", new java.util.ArrayList<Object>());
                    setFieldIfPresent(launcherApps, "mService", service);
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
            Class<?> type = Class.forName("android.app.admin.DevicePolicyManager");
            Class<?> serviceType =
                Class.forName("android.app.admin.IDevicePolicyManager");
            final IBinder binder = new Binder();
            Object service = java.lang.reflect.Proxy.newProxyInstance(
                serviceType.getClassLoader(),
                new Class<?>[] { serviceType },
                new java.lang.reflect.InvocationHandler() {
                    @Override
                    public Object invoke(Object proxy,
                                         java.lang.reflect.Method method,
                                         Object[] args) {
                        if ("asBinder".equals(method.getName())) {
                            return binder;
                        }
                        if ("getManagedSubscriptionsPolicy".equals(method.getName())) {
                            return createManagedSubscriptionsPolicy();
                        }
                        return defaultValue(method.getReturnType());
                    }
                });
            for (java.lang.reflect.Constructor<?> ctor
                     : type.getDeclaredConstructors()) {
                Class<?>[] params = ctor.getParameterTypes();
                if (params.length >= 2 && params[0] == Context.class) {
                    Object[] args = params.length == 2
                        ? new Object[] { context, service }
                        : new Object[] { context, service, Boolean.FALSE };
                    ctor.setAccessible(true);
                    return ctor.newInstance(args);
                }
            }

            Object manager = allocateWithoutConstructor(type);
            if (manager != null) {
                setFieldIfPresent(manager, "mContext", context);
                setFieldIfPresent(manager, "mService", service);
                setFieldIfPresent(manager, "mResourcesManager",
                                  createDevicePolicyResourcesManager(context,
                                                                     service));
                return manager;
            }
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] DevicePolicyManager create failed: "
                + t.getClass().getName() + ": " + t.getMessage());
            return null;
        }
        return null;
    }

    private static Object createDevicePolicyResourcesManager(Context context,
                                                            Object service) {
        try {
            Class<?> type =
                Class.forName("android.app.admin.DevicePolicyResourcesManager");
            for (java.lang.reflect.Constructor<?> ctor
                     : type.getDeclaredConstructors()) {
                Class<?>[] params = ctor.getParameterTypes();
                if (params.length == 2 && params[0] == Context.class) {
                    ctor.setAccessible(true);
                    return ctor.newInstance(context, service);
                }
            }
            Object manager = allocateWithoutConstructor(type);
            if (manager != null) {
                setFieldIfPresent(manager, "mContext", context);
                setFieldIfPresent(manager, "mService", service);
                return manager;
            }
        } catch (Throwable ignored) {
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

    private static Resources createResources(String apkPath) {
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
            "LauncherTheme",
            "AppTheme",
            "Theme",
            "BaseLauncherTheme",
            "LauncherThemeBase",
            "Theme_Launcher",
            "Theme_Launcher3",
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
}
