package com.muplar.runtime;

import java.io.File;
import java.lang.reflect.Constructor;

public final class ArtApkMain {
    private ArtApkMain() {
    }

    private static native void installTypefaceDefaultsNative();

    public static void main(String[] args) {
        String apkPath = args.length > 0 ? args[0] : "";
        String packageName = args.length > 1 ? args[1] : "";
        String launchActivity = args.length > 2 ? args[2] : "";
        String applicationClass = args.length > 3 ? args[3] : "";

        System.out.println("[Muplar/ART] ArtApkMain started");
        relaxHiddenApiChecks();
        loadFrameworkNativeRuntime();
        loadRuntimeShim();
        loadSystemFontMap();
        MuplarServices.install();
        System.out.println("[Muplar/ART] apk=" + apkPath);
        if (!packageName.isEmpty()) {
            System.out.println("[Muplar/ART] package=" + packageName);
        }
        if (!launchActivity.isEmpty()) {
            System.out.println("[Muplar/ART] launchActivity=" + launchActivity);
        }
        FrameworkProcessSession.start(packageName, launchActivity);

        try {
            prepareMainLooper();
            FrameworkDeviceController.start();
            installActivityThreadForFramework();
            ClassLoader loader = createApkClassLoader(apkPath, packageName);
            Thread.currentThread().setContextClassLoader(loader);
            System.out.println("[Muplar/ART] apk class loader ready");

            if (!launchActivity.isEmpty()) {
                String activityClassName =
                    normalizeActivityClassName(packageName, launchActivity);
                Class<?> activityClass =
                    Class.forName(activityClassName, false, loader);
                System.out.println("[Muplar/ART] loaded activity class="
                    + activityClass.getName());

                // Instantiate activity (possibly private constructor)
                java.lang.reflect.Constructor<?> ctor = activityClass.getDeclaredConstructor();
                ctor.setAccessible(true);
                Object activityObj = ctor.newInstance();
                System.out.println("[Muplar/ART] instantiated activity class");
                attachBaseContext(activityObj,
                    new MuplarContext(packageName, apkPath, loader));
                attachActivityInfo(activityObj, packageName, activityClassName, apkPath);
                attachApplication(activityObj, packageName, apkPath, loader,
                    applicationClass);
                attachWindow(activityObj, packageName, apkPath, loader);
                attachMainThread(activityObj);
                attachInstrumentation(activityObj);
                attachFragmentHost(activityObj);

                // Call onCreate lifecycle method using reflection
                try {
                    java.lang.reflect.Method onCreateMethod = null;
                    Class<?> curr = activityClass;
                    while (curr != null) {
                        try {
                            onCreateMethod = curr.getDeclaredMethod("onCreate", Class.forName("android.os.Bundle", false, loader));
                            break;
                        } catch (NoSuchMethodException e) {
                            curr = curr.getSuperclass();
                        }
                    }
                    if (onCreateMethod != null) {
                        onCreateMethod.setAccessible(true);
                        Class<?> bundleClass = Class.forName("android.os.Bundle", false, loader);
                        Object bundleObj = bundleClass.getDeclaredConstructor().newInstance();
                        onCreateMethod.invoke(activityObj, bundleObj);
                        System.out.println("[Muplar/ART] onCreate completed successfully");
                        driveActivityLifecycle(activityObj, activityClass, loader);
                        android.os.IBinder token = attachToken(activityObj);
                        FrameworkDeviceController.registerActivity(activityObj,
                            packageName, activityClassName, activityClass, token);
                        runMainLooper();
                    } else {
                        System.out.println("[Muplar/ART] onCreate method not found");
                    }
                } catch (Exception e) {
                    System.err.println("[Muplar/ART] failed to invoke onCreate: " + e.getMessage());
                    e.printStackTrace();
                }
            }
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] bootstrap failed: "
                + t.getClass().getName() + ": " + t.getMessage());
            t.printStackTrace(System.err);
            throw new RuntimeException("Muplar ART bootstrap failed", t);
        }
    }

    public static synchronized boolean launchActivity(String apkPath,
                                                      String packageName,
                                                      String launchActivity) {
        return launchActivity("", apkPath, packageName, launchActivity, "");
    }

    public static synchronized boolean launchActivity(String tabIdentifier,
                                                      String apkPath,
                                                      String packageName,
                                                      String launchActivity,
                                                      String applicationClass) {
        if (apkPath == null || apkPath.isEmpty() ||
            packageName == null || packageName.isEmpty() ||
            launchActivity == null || launchActivity.isEmpty()) {
            return false;
        }
        try {
            ClassLoader loader = createApkClassLoader(apkPath, packageName);
            Thread.currentThread().setContextClassLoader(loader);
            String activityClassName =
                normalizeActivityClassName(packageName, launchActivity);
            Class<?> activityClass =
                Class.forName(activityClassName, false, loader);
            java.lang.reflect.Constructor<?> ctor =
                activityClass.getDeclaredConstructor();
            ctor.setAccessible(true);
            Object activityObj = ctor.newInstance();
            attachBaseContext(activityObj,
                new MuplarContext(packageName, apkPath, loader));
            attachActivityInfo(activityObj, packageName, activityClassName, apkPath);
            attachApplication(activityObj, packageName, apkPath, loader,
                applicationClass);
            attachWindow(activityObj, packageName, apkPath, loader);
            attachMainThread(activityObj);
            attachInstrumentation(activityObj);
            attachFragmentHost(activityObj);

            java.lang.reflect.Method onCreateMethod = null;
            Class<?> current = activityClass;
            while (current != null) {
                try {
                    onCreateMethod = current.getDeclaredMethod(
                        "onCreate",
                        Class.forName("android.os.Bundle", false, loader));
                    break;
                } catch (NoSuchMethodException ignored) {
                    current = current.getSuperclass();
                }
            }
            if (onCreateMethod != null) {
                onCreateMethod.setAccessible(true);
                Object bundleObj = Class.forName("android.os.Bundle", false, loader)
                    .getDeclaredConstructor().newInstance();
                onCreateMethod.invoke(activityObj, bundleObj);
            }
            driveActivityLifecycle(activityObj, activityClass, loader);
            android.os.IBinder token = attachToken(activityObj);
            FrameworkDeviceController.registerActivityForTab(tabIdentifier,
                apkPath, packageName, activityClassName, activityObj,
                activityClass, token);
            System.out.println("[Muplar/ART] launched activity class="
                + activityClassName);
            return true;
        } catch (Throwable error) {
            System.err.println("[Muplar/ART] launchActivity failed: " +
                error.getClass().getName() + ": " + error.getMessage());
            error.printStackTrace(System.err);
            return false;
        }
    }

    private static void loadRuntimeShim() {
        try {
            System.load("/data/local/tmp/muplar/art/libmuplar_android_art_shim.so");
            System.out.println("[Muplar/ART] runtime shim loaded");
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] runtime shim load failed: "
                + t.getClass().getName() + ": " + t.getMessage());
        }
    }

    private static void loadFrameworkNativeRuntime() {
        try {
            System.load("/system/lib64/libandroid_runtime.so");
            System.out.println("[Muplar/ART] framework native runtime loaded");
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] framework native runtime load failed: "
                + t.getClass().getName() + ": " + t.getMessage());
        }
    }

    private static void relaxHiddenApiChecks() {
        try {
            Class<?> vmRuntime = Class.forName("dalvik.system.VMRuntime");
            java.lang.reflect.Method getRuntime =
                vmRuntime.getDeclaredMethod("getRuntime");
            Object runtime = getRuntime.invoke(null);
            java.lang.reflect.Method setHiddenApiExemptions =
                vmRuntime.getDeclaredMethod(
                    "setHiddenApiExemptions", String[].class);
            setHiddenApiExemptions.invoke(
                runtime, new Object[] { new String[] { "L" } });
            System.out.println("[Muplar/ART] hidden API checks relaxed");
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] hidden API relax failed: "
                + t.getClass().getName() + ": " + t.getMessage());
        }
    }

    private static void prepareMainLooper() {
        try {
            Class<?> looperClass = Class.forName("android.os.Looper");
            java.lang.reflect.Method myLooper =
                looperClass.getMethod("myLooper");
            if (myLooper.invoke(null) != null) {
                return;
            }
            try {
                java.lang.reflect.Method prepareMainLooper =
                    looperClass.getDeclaredMethod("prepareMainLooper");
                prepareMainLooper.setAccessible(true);
                prepareMainLooper.invoke(null);
            } catch (NoSuchMethodException missingMain) {
                java.lang.reflect.Method prepare =
                    looperClass.getDeclaredMethod("prepare");
                prepare.setAccessible(true);
                prepare.invoke(null);
            }
            System.out.println("[Muplar/ART] main looper prepared");
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] main looper prepare failed: "
                + t.getClass().getName() + ": " + t.getMessage());
        }
    }

    private static void installActivityThreadForFramework() {
        try {
            Class<?> activityThreadClass =
                Class.forName("android.app.ActivityThread");
            java.lang.reflect.Method current =
                activityThreadClass.getMethod("currentActivityThread");
            Object thread = current.invoke(null);
            if (thread == null) {
                thread = allocateWithoutConstructor(activityThreadClass);
                setStaticField(activityThreadClass, "sCurrentActivityThread", thread);
            }
            setField(thread, "mCoreSettingsLock", new Object());
            setField(thread, "mCoreSettings",
                Class.forName("android.os.Bundle").getDeclaredConstructor().newInstance());
            installConfigurationController(thread);
            try {
                Class<?> idsClass = Class.forName("android.app.IdsController");
                Object ids = idsClass
                    .getConstructor(Class.forName("android.content.Context"))
                    .newInstance(new Object[] { null });
                setField(thread, "mIdsController", ids);
            } catch (Throwable idsError) {
                System.err.println("[Muplar/ART] IDS controller install skipped: "
                    + idsError.getClass().getName() + ": "
                    + idsError.getMessage());
            }
            System.out.println("[Muplar/ART] ActivityThread installed");
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] ActivityThread install failed: "
                + t.getClass().getName() + ": " + t.getMessage());
        }
    }

    private static void installConfigurationController(Object activityThread) {
        try {
            Object config = Class.forName("android.content.res.Configuration")
                .getDeclaredConstructor().newInstance();
            Class<?> controllerClass =
                Class.forName("android.app.ConfigurationController");
            java.lang.reflect.Constructor<?> ctor = controllerClass
                .getDeclaredConstructor(
                    Class.forName("android.app.ActivityThreadInternal"));
            ctor.setAccessible(true);
            Object controller = ctor.newInstance(activityThread);
            try {
                java.lang.reflect.Method setConfiguration =
                    controllerClass.getDeclaredMethod(
                        "setConfiguration", config.getClass());
                setConfiguration.setAccessible(true);
                setConfiguration.invoke(controller, config);
            } catch (NoSuchMethodException ignored) {
            }
            try {
                java.lang.reflect.Method setCompatConfiguration =
                    controllerClass.getDeclaredMethod(
                        "setCompatConfiguration", config.getClass());
                setCompatConfiguration.setAccessible(true);
                setCompatConfiguration.invoke(controller, config);
            } catch (NoSuchMethodException ignored) {
            }
            setField(activityThread, "mConfigurationController", controller);
            setField(activityThread, "mConfiguration", config);
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] configuration controller install failed: "
                + t.getClass().getName() + ": " + t.getMessage());
        }
    }

    private static void installTypefaceDefaults() {
        try {
            installTypefaceDefaultsNative();
            System.out.println("[Muplar/ART] Typeface defaults installed");
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] Typeface defaults install failed: "
                + t.getClass().getName() + ": " + t.getMessage());
        }
    }

    private static void loadSystemFontMap() {
        try {
            Class<?> typeface = Class.forName("android.graphics.Typeface");
            java.lang.reflect.Method method =
                typeface.getDeclaredMethod("loadPreinstalledSystemFontMap");
            method.setAccessible(true);
            method.invoke(null);
            repairSystemFontDefaults(typeface);
            System.out.println("[Muplar/ART] system font map loaded");
        } catch (Throwable t) {
            Throwable detail = t instanceof java.lang.reflect.InvocationTargetException
                && ((java.lang.reflect.InvocationTargetException) t).getCause() != null
                    ? ((java.lang.reflect.InvocationTargetException) t).getCause()
                    : t;
            System.err.println("[Muplar/ART] system font map load failed: "
                + detail.getClass().getName() + ": " + detail.getMessage());
            detail.printStackTrace(System.err);
        }
    }

    @SuppressWarnings("unchecked")
    private static void repairSystemFontDefaults(Class<?> typeface) throws Exception {
        java.lang.reflect.Method getSystemFontMap =
            typeface.getDeclaredMethod("getSystemFontMap");
        getSystemFontMap.setAccessible(true);
        java.util.Map<String, Object> map =
            (java.util.Map<String, Object>) getSystemFontMap.invoke(null);
        Object sans = firstNonNull(
            map.get("sans-serif"), map.get("sans"), map.values().isEmpty()
                ? null : map.values().iterator().next());
        if (sans == null) {
            return;
        }
        Object serif = firstNonNull(map.get("serif"), sans);
        Object monospace = firstNonNull(map.get("monospace"), sans);

        java.lang.reflect.Method create =
            typeface.getDeclaredMethod("create", typeface, Integer.TYPE);
        Object bold = create.invoke(null, sans, Integer.valueOf(1));
        Object italic = create.invoke(null, sans, Integer.valueOf(2));
        Object boldItalic = create.invoke(null, sans, Integer.valueOf(3));

        java.util.List<Object> defaults = java.util.Arrays.asList(
            sans, bold, italic, boldItalic);
        java.util.List<Object> generics = java.util.Arrays.asList(
            sans, serif, monospace);
        java.lang.reflect.Method change =
            typeface.getDeclaredMethod(
                "changeDefaultFontForTest",
                java.util.List.class,
                java.util.List.class);
        change.setAccessible(true);
        change.invoke(null, defaults, generics);
        for (String fieldName : new String[]{"DEFAULT", "DEFAULT_BOLD", "sDefaultFlipfont"}) {
            try {
                java.lang.reflect.Field f = typeface.getDeclaredField(fieldName);
                f.setAccessible(true);
                Object val = f.get(null);
                System.out.println("[Muplar/ART] Typeface." + fieldName + "=" + val);
                if (val == null) {
                    f.set(null, fieldName.contains("BOLD") ? bold : sans);
                    System.out.println("[Muplar/ART] Initialized Typeface." + fieldName);
                }
            } catch (Throwable t) {
                System.err.println("[Muplar/ART] Typeface." + fieldName + " setup failed: " + t);
            }
        }
        try {
            java.lang.reflect.Field flipFontPath = typeface.getDeclaredField("FlipFontPath");
            flipFontPath.setAccessible(true);
            System.out.println("[Muplar/ART] FlipFontPath before=" + flipFontPath.get(null));
            flipFontPath.set(null, "default");
            System.out.println("[Muplar/ART] FlipFontPath set to default");
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] FlipFontPath setup failed: " + t);
        }
        System.out.println("[Muplar/ART] system font defaults repaired");
    }

    private static Object firstNonNull(Object... values) {
        for (Object value : values) {
            if (value != null) {
                return value;
            }
        }
        return null;
    }

    private static void driveActivityLifecycle(Object activityObj,
                                               Class<?> activityClass,
                                               ClassLoader loader)
        throws Exception {
        java.lang.reflect.Method onStart =
            findLifecycleMethod(activityClass, "onStart");
        if (onStart != null) {
            onStart.setAccessible(true);
            onStart.invoke(activityObj);
        }

        try {
            java.lang.reflect.Method onPostCreate =
                findLifecycleMethod(activityClass, "onPostCreate",
                    Class.forName("android.os.Bundle", false, loader));
            if (onPostCreate != null) {
                onPostCreate.setAccessible(true);
                Object bundleObj = Class.forName("android.os.Bundle", false, loader)
                    .getDeclaredConstructor().newInstance();
                onPostCreate.invoke(activityObj, bundleObj);
            }
        } catch (NoSuchMethodException ignored) {
        }

        java.lang.reflect.Method onResume =
            findLifecycleMethod(activityClass, "onResume");
        if (onResume != null) {
            onResume.setAccessible(true);
            onResume.invoke(activityObj);
        }
        System.out.println("[Muplar/ART] onStart/onResume completed successfully");

        try {
            Object decor = getField(activityObj, "mDecor");
            if (decor == null) {
                Object window = getField(activityObj, "mWindow");
                if (window != null) {
                    java.lang.reflect.Method getDecorView =
                        window.getClass().getMethod("getDecorView");
                    decor = getDecorView.invoke(window);
                    if (decor != null) {
                        setField(activityObj, "mDecor", decor);
                    }
                }
            }
            java.lang.reflect.Method makeVisible =
                Class.forName("android.app.Activity").getDeclaredMethod("makeVisible");
            makeVisible.setAccessible(true);
            makeVisible.invoke(activityObj);
            System.out.println("[Muplar/ART] makeVisible completed successfully");
            if (decor instanceof android.view.View) {
                MuplarFramePresenter.schedule((android.view.View) decor);
                MuplarScreenshot.captureIfRequested((android.view.View) decor);
            }
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] failed to invoke makeVisible: " + t.getMessage());
            t.printStackTrace();
        }
    }

    private static java.lang.reflect.Method findLifecycleMethod(
        Class<?> type,
        String name,
        Class<?>... parameterTypes)
        throws NoSuchMethodException {
        Class<?> current = type;
        while (current != null) {
            try {
                return current.getDeclaredMethod(name, parameterTypes);
            } catch (NoSuchMethodException ignored) {
                current = current.getSuperclass();
            }
        }
        throw new NoSuchMethodException(name);
    }

    private static void runMainLooper() {
        try {
            System.out.println("[Muplar/ART] entering main looper");
            Class<?> looperClass = Class.forName("android.os.Looper");
            java.lang.reflect.Method loop =
                looperClass.getDeclaredMethod("loop");
            loop.setAccessible(true);
            loop.invoke(null);
        } catch (Throwable t) {
            Throwable detail = t instanceof java.lang.reflect.InvocationTargetException
                && ((java.lang.reflect.InvocationTargetException)t).getCause() != null
                    ? ((java.lang.reflect.InvocationTargetException)t).getCause()
                    : t;
            System.err.println("[Muplar/ART] main looper exited: "
                + detail.getClass().getName() + ": " + detail.getMessage());
            detail.printStackTrace(System.err);
        }
    }

    private static void attachBaseContext(Object activityObj, Object context) {
        try {
            Class<?> contextWrapper =
                Class.forName("android.content.ContextWrapper");
            java.lang.reflect.Method attachBaseContext =
                contextWrapper.getDeclaredMethod(
                    "attachBaseContext",
                    Class.forName("android.content.Context"));
            attachBaseContext.setAccessible(true);
            attachBaseContext.invoke(activityObj, context);
            setField(activityObj, "mBase", context);
            if (context instanceof android.content.Context) {
                setField(activityObj, "mTheme",
                    ((android.content.Context) context).getTheme());
            }
            System.out.println("[Muplar/ART] base context attached");
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] base context attach failed: "
                + t.getClass().getName() + ": " + t.getMessage());
            t.printStackTrace(System.err);
            try {
                setField(activityObj, "mBase", context);
                if (context instanceof android.content.Context) {
                    setField(activityObj, "mTheme",
                        ((android.content.Context) context).getTheme());
                }
                System.out.println("[Muplar/ART] base context field attached");
            } catch (Throwable fieldError) {
                System.err.println("[Muplar/ART] base context field attach failed: "
                    + fieldError.getClass().getName() + ": "
                    + fieldError.getMessage());
            }
        }
        try {
            setField(activityObj, "mBase", context);
        } catch (Throwable ignored) {
        }
    }

    private static void attachActivityInfo(Object activityObj,
                                           String packageName,
                                           String activityName,
                                           String apkPath) {
        try {
            Class<?> activityInfoClass =
                Class.forName("android.content.pm.ActivityInfo");
            Object info = activityInfoClass.getDeclaredConstructor().newInstance();
            setField(info, "packageName", packageName);
            setField(info, "name", activityName);
            setField(info, "parentActivityName", null);
            int theme = 0;

            Class<?> applicationInfoClass =
                Class.forName("android.content.pm.ApplicationInfo");
            Object appInfo = applicationInfoClass.getDeclaredConstructor().newInstance();
            setField(appInfo, "packageName", packageName);
            setField(appInfo, "sourceDir", apkPath);
            setField(appInfo, "publicSourceDir", apkPath);
            setField(appInfo, "dataDir", "/data/user/0/" + packageName);
            setField(appInfo, "uid", Integer.valueOf(1000));
            setField(appInfo, "flags", Integer.valueOf(1));
            setField(appInfo, "targetSdkVersion", Integer.valueOf(30));
            try {
                android.content.res.Resources resources =
                    new MuplarContext(packageName, apkPath,
                        ArtApkMain.class.getClassLoader()).getResources();
                theme = resources.getIdentifier(
                    "LauncherTheme", "style", packageName);
                if (theme == 0) {
                    theme = resources.getIdentifier("AppTheme", "style", packageName);
                }
                if (theme != 0) {
                    setField(appInfo, "theme", Integer.valueOf(theme));
                    setField(info, "theme", Integer.valueOf(theme));
                }
            } catch (Throwable ignored) {
            }
            setField(info, "applicationInfo", appInfo);

            setField(activityObj, "mActivityInfo", info);
            System.out.println("[Muplar/ART] activity info attached theme=0x"
                + Integer.toHexString(theme));
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] activity info attach failed: "
                + t.getClass().getName() + ": " + t.getMessage());
        }
    }

    private static void attachFragmentHost(Object activityObj) {
        try {
            Object fragments = getField(activityObj, "mFragments");
            if (fragments == null) {
                return;
            }
            java.lang.reflect.Method attachHost =
                fragments.getClass().getMethod(
                    "attachHost", Class.forName("android.app.Fragment"));
            attachHost.invoke(fragments, new Object[] { null });
            System.out.println("[Muplar/ART] fragment host attached");
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] fragment host attach failed: "
                + t.getClass().getName() + ": " + t.getMessage());
        }
    }

    private static final java.util.Map<String, ClassLoader> packageClassLoaders =
        new java.util.concurrent.ConcurrentHashMap<String, ClassLoader>();
    private static final java.util.Map<String, Object> packageApplications =
        new java.util.concurrent.ConcurrentHashMap<String, Object>();

    private static android.os.IBinder attachToken(Object activityObj) {
        try {
            android.os.IBinder token = new android.os.Binder();
            setField(activityObj, "mToken", token);
            System.out.println("[Muplar/ART] token attached: " + token);
            return token;
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] token attach failed: " + t.getMessage());
            return null;
        }
    }

    private static void attachApplication(Object activityObj,
                                          String packageName,
                                          String apkPath,
                                          ClassLoader loader,
                                          String applicationClass) {
        try {
            Object application = packageApplications.get(packageName);
            Object context = null;
            if (application == null) {
                if (applicationClass == null || applicationClass.isEmpty()) {
                    MuplarServices.InstalledPackage pkg = MuplarServices.findInstalledPackage(packageName);
                    if (pkg != null && pkg.application != null && !pkg.application.isEmpty()) {
                        applicationClass = pkg.application;
                    }
                }
                context = new MuplarContext(packageName, apkPath, loader);
                application = createApplication(packageName, context, loader,
                    applicationClass);
                packageApplications.put(packageName, application);
            }
            if (context instanceof MuplarContext && application instanceof android.content.Context) {
                ((MuplarContext) context).setApplicationContext((android.content.Context) application);
            }
            try {
                Object baseContext = getField(activityObj, "mBase");
                if (baseContext instanceof MuplarContext && application instanceof android.content.Context) {
                    ((MuplarContext) baseContext).setApplicationContext((android.content.Context) application);
                }
            } catch (Throwable ignored) {
            }
            setField(activityObj, "mApplication", application);
            attachApplicationToActivityThread(application);
            System.out.println("[Muplar/ART] application attached");
        } catch (Throwable t) {
            Throwable cause = t instanceof java.lang.reflect.InvocationTargetException
                ? ((java.lang.reflect.InvocationTargetException)t).getCause()
                : t;
            System.err.println("[Muplar/ART] application attach failed: "
                + (cause != null ? cause.getClass().getName() + ": " + cause.getMessage() : t.getMessage()));
            if (cause != null) cause.printStackTrace(System.err);
            else t.printStackTrace(System.err);
        }
    }

    private static Object createApplication(String packageName,
                                            Object context,
                                            ClassLoader loader,
                                            String applicationClass)
        throws Exception {
        Object application;
        String className = applicationClass == null ? "" : applicationClass;
        if (className.isEmpty()) {
            application =
                new MuplarApplication((android.content.Context)context);
        } else {
            Class<?> type = Class.forName(
                normalizeActivityClassName(packageName, className), false,
                loader);
            Constructor<?> constructor = type.getDeclaredConstructor();
            constructor.setAccessible(true);
            application = constructor.newInstance();
        }
        attachApplicationBaseContext(application, context);
        if (context instanceof MuplarContext && application instanceof android.content.Context) {
            ((MuplarContext) context).setApplicationContext((android.content.Context) application);
        }
        if (!className.isEmpty()) {
            java.lang.reflect.Method onCreate =
                application.getClass().getMethod("onCreate");
            onCreate.invoke(application);
            System.out.println("[Muplar/ART] Application onCreate completed class="
                + application.getClass().getName());
        }
        return application;
    }

    private static void attachApplicationBaseContext(Object application,
                                                     Object context) {
        try {
            try {
                java.lang.reflect.Method attachBaseContext =
                    Class.forName("android.content.ContextWrapper")
                        .getDeclaredMethod(
                            "attachBaseContext",
                            Class.forName("android.content.Context"));
                attachBaseContext.setAccessible(true);
                attachBaseContext.invoke(application, context);
            } catch (Throwable ignored) {
                try {
                    setField(application, "mBase", context);
                } catch (Throwable ignoredField) {
                }
            }
            setField(application, "mBase", context);
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] application base attach failed: "
                + t.getClass().getName() + ": " + t.getMessage());
        }
    }

    @SuppressWarnings("unchecked")
    private static void attachApplicationToActivityThread(Object application) {
        try {
            Class<?> activityThreadClass =
                Class.forName("android.app.ActivityThread");
            java.lang.reflect.Method current =
                activityThreadClass.getMethod("currentActivityThread");
            Object thread = current.invoke(null);
            if (thread == null) {
                return;
            }
            setField(thread, "mInitialApplication", application);
            try {
                Object allApplications = getField(thread, "mAllApplications");
                if (!(allApplications instanceof java.util.List)) {
                    allApplications = new java.util.ArrayList<Object>();
                    setField(thread, "mAllApplications", allApplications);
                }
                java.util.List<Object> list =
                    (java.util.List<Object>)allApplications;
                if (!list.contains(application)) {
                    list.add(application);
                }
            } catch (Throwable ignored) {
            }
            System.out.println("[Muplar/ART] ActivityThread application attached");
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] ActivityThread application attach failed: "
                + t.getClass().getName() + ": " + t.getMessage());
        }
    }

    private static void attachWindow(Object activityObj,
                                     String packageName,
                                     String apkPath,
                                     ClassLoader loader) {
        try {
            // Must be activityObj itself, not a fresh/separate MuplarContext:
            // AOSP code (e.g. WindowOnBackInvokedDispatcher.
            // isOnBackInvokedCallbackEnabled) walks a window/view's context
            // up through ContextWrapper.getBaseContext() looking for the
            // enclosing Activity, and stops there. A standalone MuplarContext
            // is never that Activity, and its own getBaseContext() returns
            // null (built via super(null)), so the walk falls off the end
            // and NPEs calling getApplicationInfo() on null.
            android.content.Context context =
                (android.content.Context) activityObj;
            android.content.Context baseContext =
                (activityObj instanceof android.content.ContextWrapper)
                    ? ((android.content.ContextWrapper) activityObj).getBaseContext()
                    : context;
            if (baseContext == null) {
                baseContext = context;
            }
            Object window = null;
            try {
                window = Class.forName("com.android.internal.policy.PhoneWindow")
                    .getConstructor(android.content.Context.class)
                    .newInstance(context);
            } catch (Throwable t) {
                System.err.println("[Muplar/ART] PhoneWindow instantiation failed: " + t);
            }
            Object windowManager =
                baseContext.getSystemService(android.content.Context.WINDOW_SERVICE);
            if (windowManager != null) {
                try {
                    java.lang.reflect.Method setWindowManager =
                        Class.forName("android.view.Window").getMethod(
                            "setWindowManager",
                            Class.forName("android.view.WindowManager"),
                            Class.forName("android.os.IBinder"),
                            String.class,
                            Boolean.TYPE);
                    Object token = null;
                    try {
                        token = getField(activityObj, "mToken");
                    } catch (Throwable ignored) {
                    }
                    if (token == null) token = new android.os.Binder();
                    setWindowManager.invoke(
                        window,
                        windowManager,
                        token,
                        packageName,
                        Boolean.FALSE);
                } catch (Throwable t) {
                    System.err.println("[Muplar/ART] Window.setWindowManager fallback: " + t);
                }
                Object wm = null;
                try {
                    java.lang.reflect.Method getWindowManager =
                        Class.forName("android.view.Window").getMethod(
                            "getWindowManager");
                    wm = getWindowManager.invoke(window);
                } catch (Throwable ignored) {
                }
                setField(activityObj, "mWindowManager", wm != null ? wm : windowManager);
            }
            setField(activityObj, "mWindow", window);
            System.out.println("[Muplar/ART] window attached");
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] window attach failed: "
                + t.getClass().getName() + ": " + t.getMessage());
            t.printStackTrace(System.err);
        }
    }

    private static void attachMainThread(Object activityObj) {
        try {
            Class<?> activityThreadClass =
                Class.forName("android.app.ActivityThread");
            java.lang.reflect.Method current =
                activityThreadClass.getMethod("currentActivityThread");
            Object thread = current.invoke(null);
            if (thread == null) {
                return;
            }
            setField(activityObj, "mMainThread", thread);
            System.out.println("[Muplar/ART] main thread attached");
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] main thread attach failed: "
                + t.getClass().getName() + ": " + t.getMessage());
        }
    }

    private static void attachInstrumentation(Object activityObj) {
        try {
            Object instrumentation =
                Class.forName("android.app.Instrumentation")
                    .getDeclaredConstructor().newInstance();
            setField(activityObj, "mInstrumentation", instrumentation);

            Class<?> activityThreadClass =
                Class.forName("android.app.ActivityThread");
            java.lang.reflect.Method current =
                activityThreadClass.getMethod("currentActivityThread");
            Object thread = current.invoke(null);
            if (thread != null) {
                setField(thread, "mInstrumentation", instrumentation);
            }
            System.out.println("[Muplar/ART] instrumentation attached");
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] instrumentation attach failed: "
                + t.getClass().getName() + ": " + t.getMessage());
        }
    }

    private static Object getField(Object target, String name)
        throws Exception {
        Class<?> type = target.getClass();
        while (type != null) {
            try {
                java.lang.reflect.Field field = type.getDeclaredField(name);
                field.setAccessible(true);
                return field.get(target);
            } catch (NoSuchFieldException ignored) {
                type = type.getSuperclass();
            }
        }
        throw new NoSuchFieldException(name);
    }

    private static void setField(Object target, String name, Object value)
        throws Exception {
        Class<?> type = target.getClass();
        while (type != null) {
            try {
                java.lang.reflect.Field field = type.getDeclaredField(name);
                field.setAccessible(true);
                field.set(target, value);
                return;
            } catch (NoSuchFieldException ignored) {
                type = type.getSuperclass();
            }
        }
        throw new NoSuchFieldException(name);
    }

    private static void setStaticField(Class<?> type, String name, Object value)
        throws Exception {
        while (type != null) {
            try {
                java.lang.reflect.Field field = type.getDeclaredField(name);
                field.setAccessible(true);
                try {
                    field.set(null, value);
                } catch (IllegalAccessException finalField) {
                    setStaticFieldWithUnsafe(field, value);
                }
                return;
            } catch (NoSuchFieldException ignored) {
                type = type.getSuperclass();
            }
        }
        throw new NoSuchFieldException(name);
    }

    private static void setStaticFieldWithUnsafe(java.lang.reflect.Field field,
                                                 Object value)
        throws Exception {
        java.lang.reflect.Field unsafeField =
            Class.forName("sun.misc.Unsafe").getDeclaredField("theUnsafe");
        unsafeField.setAccessible(true);
        Object unsafe = unsafeField.get(null);
        Object base = unsafe.getClass()
            .getMethod("staticFieldBase", java.lang.reflect.Field.class)
            .invoke(unsafe, field);
        long offset = ((Long) unsafe.getClass()
            .getMethod("staticFieldOffset", java.lang.reflect.Field.class)
            .invoke(unsafe, field)).longValue();
        Class<?> fieldType = field.getType();
        if (fieldType == Integer.TYPE) {
            unsafe.getClass()
                .getMethod("putInt", Object.class, Long.TYPE, Integer.TYPE)
                .invoke(unsafe, base, Long.valueOf(offset), value);
        } else if (fieldType == Long.TYPE) {
            unsafe.getClass()
                .getMethod("putLong", Object.class, Long.TYPE, Long.TYPE)
                .invoke(unsafe, base, Long.valueOf(offset), value);
        } else if (fieldType == Boolean.TYPE) {
            unsafe.getClass()
                .getMethod("putBoolean", Object.class, Long.TYPE, Boolean.TYPE)
                .invoke(unsafe, base, Long.valueOf(offset), value);
        } else {
            unsafe.getClass()
                .getMethod("putObject", Object.class, Long.TYPE, Object.class)
                .invoke(unsafe, base, Long.valueOf(offset), value);
        }
    }

    private static Object allocateWithoutConstructor(Class<?> type)
        throws Exception {
        java.lang.reflect.Field field =
            Class.forName("sun.misc.Unsafe").getDeclaredField("theUnsafe");
        field.setAccessible(true);
        Object unsafe = field.get(null);
        java.lang.reflect.Method allocateInstance =
            unsafe.getClass().getMethod("allocateInstance", Class.class);
        return allocateInstance.invoke(unsafe, type);
    }

    private static ClassLoader createApkClassLoader(
        String apkPath,
        String packageName) throws Exception {
        if (apkPath.isEmpty()) {
            throw new IllegalArgumentException("APK path is required");
        }
        if (!packageName.isEmpty() && packageClassLoaders.containsKey(packageName)) {
            return packageClassLoaders.get(packageName);
        }

        ClassLoader parent = Thread.currentThread().getContextClassLoader();
        if (parent == null) {
            parent = ArtApkMain.class.getClassLoader();
        }

        // 1st choice: dalvik.system.DexClassLoader  (running in ART)
        try {
            Class<?> dexClassLoader =
                Class.forName("dalvik.system.DexClassLoader");
            Constructor<?> ctor = dexClassLoader.getConstructor(
                String.class, String.class, String.class, ClassLoader.class);
            ClassLoader loader = (ClassLoader) ctor.newInstance(
                apkPath, ensureDexOptDir(packageName), null, parent);
            System.out.println("[Muplar/ART] classLoader="
                + dexClassLoader.getName());
            if (!packageName.isEmpty()) {
                packageClassLoaders.put(packageName, loader);
            }
            return loader;
        } catch (ClassNotFoundException ignored) {
            // Not running in ART — fall through.
        }

        // 2nd choice: dalvik.system.PathClassLoader  (older ART)
        try {
            Class<?> pathClassLoader =
                Class.forName("dalvik.system.PathClassLoader");
            Constructor<?> ctor = pathClassLoader.getConstructor(
                String.class, ClassLoader.class);
            ClassLoader loader =
                (ClassLoader) ctor.newInstance(apkPath, parent);
            System.out.println("[Muplar/ART] classLoader="
                + pathClassLoader.getName());
            if (!packageName.isEmpty()) {
                packageClassLoaders.put(packageName, loader);
            }
            return loader;
        } catch (ClassNotFoundException ignored) {
            // Not running in ART — fall through.
        }

        throw new IllegalStateException(
            "ART DexClassLoader/PathClassLoader is unavailable");
    }

    private static String ensureDexOptDir(String packageName) {
        String base;
        String artBase = "/data/local/tmp/muplar/art/dexopt";
        File artParent = new File(artBase).getParentFile();
        if (artParent != null && artParent.canWrite()) {
            base = artBase;
        } else {
            base = System.getProperty("java.io.tmpdir",
                       System.getProperty("user.home"))
                 + "/muplar/art/dexopt";
        }
        String dirName = packageName.isEmpty() ? "default" : sanitize(packageName);
        File dir = new File(base + "/" + dirName);
        if (!dir.exists() && !dir.mkdirs()) {
            throw new IllegalStateException(
                "unable to create dexopt directory: " + dir.getAbsolutePath());
        }
        return dir.getAbsolutePath();
    }

    private static String normalizeActivityClassName(
        String packageName,
        String activityName) {
        if (activityName.startsWith(".")) {
            return packageName + activityName;
        }
        if (activityName.indexOf('.') < 0 && !packageName.isEmpty()) {
            return packageName + "." + activityName;
        }
        return activityName;
    }

    private static String sanitize(String value) {
        StringBuilder out = new StringBuilder(value.length());
        for (int i = 0; i < value.length(); ++i) {
            char c = value.charAt(i);
            if ((c >= 'a' && c <= 'z') ||
                (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') ||
                c == '.' || c == '-' || c == '_') {
                out.append(c);
            } else {
                out.append('_');
            }
        }
        return out.toString();
    }
}
