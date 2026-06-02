package com.muplar.runtime;

import java.io.File;
import java.lang.reflect.Constructor;

public final class ArtApkMain {
    private ArtApkMain() {
    }

    public static void main(String[] args) {
        String apkPath = args.length > 0 ? args[0] : "";
        String packageName = args.length > 1 ? args[1] : "";
        String launchActivity = args.length > 2 ? args[2] : "";

        System.out.println("[Muplar/ART] ArtApkMain started");
        System.out.println("[Muplar/ART] apk=" + apkPath);
        if (!packageName.isEmpty()) {
            System.out.println("[Muplar/ART] package=" + packageName);
        }
        if (!launchActivity.isEmpty()) {
            System.out.println("[Muplar/ART] launchActivity=" + launchActivity);
        }

        try {
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
        } catch (ClassNotFoundException e) {
            Class<?> pathClassLoader =
                Class.forName("dalvik.system.PathClassLoader");
            Constructor<?> ctor = pathClassLoader.getConstructor(
                String.class, ClassLoader.class);
            ClassLoader loader =
                (ClassLoader) ctor.newInstance(apkPath, parent);
            System.out.println("[Muplar/ART] classLoader="
                + pathClassLoader.getName());
            return loader;
        }
    }

    private static String ensureDexOptDir(String packageName) {
        String dirName = packageName.isEmpty() ? "default" : sanitize(packageName);
        File dir = new File("/data/local/tmp/muplar/art/dexopt/" + dirName);
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
