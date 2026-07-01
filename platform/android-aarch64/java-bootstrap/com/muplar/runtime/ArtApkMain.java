package com.muplar.runtime;

import java.io.File;
import java.lang.reflect.Constructor;
import java.net.URL;
import java.net.URLClassLoader;

public final class ArtApkMain {
    private ArtApkMain() {
    }

    public static void main(String[] args) {
        String apkPath = args.length > 0 ? args[0] : "";
        String packageName = args.length > 1 ? args[1] : "";
        String launchActivity = args.length > 2 ? args[2] : "";
        String applicationClass = args.length > 3 ? args[3] : "";

        System.out.println("[Muplar/ART] ArtApkMain started");
        System.out.println("[Muplar/ART] apk=" + apkPath);
        if (!packageName.isEmpty()) {
            System.out.println("[Muplar/ART] package=" + packageName);
        }
        if (!launchActivity.isEmpty()) {
            System.out.println("[Muplar/ART] launchActivity=" + launchActivity);
        }
        FrameworkProcessSession.start(packageName, launchActivity);

        try {
            ClassLoader loader = createApkClassLoader(apkPath, packageName);
            Thread.currentThread().setContextClassLoader(loader);
            System.out.println("[Muplar/ART] apk class loader ready");

            if (!applicationClass.isEmpty()) {
                Class<?> type = Class.forName(
                    normalizeActivityClassName(packageName, applicationClass),
                    false, loader);
                Constructor<?> constructor = type.getDeclaredConstructor();
                constructor.setAccessible(true);
                Object application = constructor.newInstance();
                java.lang.reflect.Method onCreate =
                    type.getMethod("onCreate");
                onCreate.invoke(application);
                System.out.println("[Muplar/ART] Application onCreate completed class="
                    + type.getName());
            }

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
                        try {
                            java.lang.reflect.Method lifecycle =
                                activityClass.getMethod("dispatchStartAndResume");
                            lifecycle.invoke(activityObj);
                            System.out.println("[Muplar/ART] onStart/onResume completed successfully");
                        } catch (NoSuchMethodException ignored) {
                            // Older bootstrap Activity surface.
                        }
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

    private static ClassLoader createApkClassLoader(
        String apkPath,
        String packageName) throws Exception {
        if (apkPath.isEmpty()) {
            throw new IllegalArgumentException("APK path is required");
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
            return loader;
        } catch (ClassNotFoundException ignored) {
            // Not running in ART — fall through.
        }

        // 3rd choice: java.net.URLClassLoader  (running in host JDK via libjvm)
        // The APK has been pre-converted from DEX to a plain JAR by d8
        // (host_jvm_launcher.cpp calls convert_apk_dex_to_jar() before launch).
        URL apkUrl = new File(apkPath).toURI().toURL();
        ClassLoader loader = new URLClassLoader(new URL[]{apkUrl}, parent);
        System.out.println("[Muplar/ART] classLoader=URLClassLoader (host JDK)");
        return loader;
    }

    private static String ensureDexOptDir(String packageName) {
        // Portable scratch dir: works in ART (/data/local/tmp) and host JDK.
        String base;
        String artBase = "/data/local/tmp/muplar/art/dexopt";
        File artParent = new File(artBase).getParentFile();
        if (artParent != null && artParent.canWrite()) {
            base = artBase;
        } else {
            // Host JDK: java.io.tmpdir is set by host_jvm_launcher.cpp
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
