package com.muplar.runtime;

import android.os.Binder;
import android.os.IBinder;
import android.os.IInterface;
import android.os.Parcel;
import android.os.RemoteException;
import java.io.File;
import java.io.FileDescriptor;
import java.lang.reflect.Array;
import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.Executor;

public final class MuplarServices {
    private static final Map<String, IBinder> services = new HashMap<>();
    private static ClassLoader servicesClassLoader;

    private MuplarServices() {
    }

    public static void install() {
        try {
            Class<?> serviceManager = Class.forName("android.os.ServiceManager");
            Field field = serviceManager.getDeclaredField("sServiceManager");
            field.setAccessible(true);
            if (field.get(null) != null) {
                return;
            }
            Class<?> iface = Class.forName("android.os.IServiceManager");
            Object proxy = Proxy.newProxyInstance(
                iface.getClassLoader(),
                new Class<?>[] { iface },
                new ServiceManagerHandler());
            field.set(null, proxy);
            System.out.println("[Muplar/ART] ServiceManager installed");
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] ServiceManager install failed: "
                + t.getClass().getName() + ": " + t.getMessage());
        }
    }

    private static IBinder getBinder(String name) {
        String key = name == null ? "" : name;
        IBinder binder = services.get(key);
        if (binder == null) {
            System.out.println("[Muplar/ART] create service binder name=" + key);
            binder = createBinder(key);
            services.put(key, binder);
        }
        return binder;
    }

    private static IBinder createBinder(String name) {
        String iface = serviceInterface(name);
        if (iface == null) {
            return new Binder();
        }
        try {
            Class<?> type = resolveInterfaceClass(iface);
            Binder binder = new Binder();
            IInterface owner = (IInterface)Proxy.newProxyInstance(
                type.getClassLoader(),
                new Class<?>[] { type },
                new LocalInterfaceHandler(binder, iface));
            binder.attachInterface(owner, iface);
            return binder;
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] service binder fallback name="
                + name + " iface=" + iface + " error="
                + t.getClass().getName() + ": " + t.getMessage());
            return new NoNativeBinder(iface);
        }
    }

    private static String serviceInterface(String name) {
        if ("content".equals(name)) {
            return "android.content.IContentService";
        }
        if ("activity".equals(name)) {
            return "android.app.IActivityManager";
        }
        if ("activity_task".equals(name)) {
            return "android.app.IActivityTaskManager";
        }
        if ("package".equals(name)) {
            return "android.content.pm.IPackageManager";
        }
        if ("window".equals(name)) {
            return "android.view.IWindowManager";
        }
        if ("display".equals(name)) {
            return "android.hardware.display.IDisplayManager";
        }
        if ("permission".equals(name)) {
            return "android.os.IPermissionController";
        }
        if ("user".equals(name)) {
            return "android.os.IUserManager";
        }
        if ("device_policy".equals(name)) {
            return "android.app.admin.IDevicePolicyManager";
        }
        if ("shortcut".equals(name)) {
            return "android.content.pm.IShortcutService";
        }
        if ("launcherapps".equals(name)) {
            return "android.content.pm.ILauncherApps";
        }
        if ("wallpaper".equals(name)) {
            return "android.app.IWallpaperManager";
        }
        if ("statusbar".equals(name)) {
            return "com.android.internal.statusbar.IStatusBarService";
        }
        if ("notification".equals(name)) {
            return "android.app.INotificationManager";
        }
        if ("accessibility".equals(name)) {
            return "android.view.accessibility.IAccessibilityManager";
        }
        if ("input".equals(name)) {
            return "android.hardware.input.IInputManager";
        }
        return null;
    }

    private static Class<?> resolveInterfaceClass(String name)
        throws Exception {
        try {
            return Class.forName(name);
        } catch (ClassNotFoundException | NoClassDefFoundError bootMiss) {
            ClassLoader loader = frameworkServicesClassLoader();
            return Class.forName(name, false, loader);
        }
    }

    private static ClassLoader frameworkServicesClassLoader()
        throws Exception {
        if (servicesClassLoader != null) {
            return servicesClassLoader;
        }

        String dexOpt = "/data/local/tmp/muplar/art/dexopt/framework";
        new File(dexOpt).mkdirs();
        ClassLoader parent = MuplarServices.class.getClassLoader();
        Class<?> dexClassLoader = Class.forName("dalvik.system.DexClassLoader");
        Constructor<?> ctor = dexClassLoader.getConstructor(
            String.class, String.class, String.class, ClassLoader.class);
        servicesClassLoader = (ClassLoader)ctor.newInstance(
            "/system/framework/services.jar", dexOpt, null, parent);
        System.out.println("[Muplar/ART] services class loader ready");
        return servicesClassLoader;
    }

    private static Object defaultValue(Class<?> returnType) {
        if (returnType == Void.TYPE) {
            return null;
        }
        if (returnType == Boolean.TYPE) {
            return Boolean.FALSE;
        }
        if (returnType == Byte.TYPE) {
            return Byte.valueOf((byte)0);
        }
        if (returnType == Short.TYPE) {
            return Short.valueOf((short)0);
        }
        if (returnType == Integer.TYPE) {
            return Integer.valueOf(0);
        }
        if (returnType == Long.TYPE) {
            return Long.valueOf(0);
        }
        if (returnType == Float.TYPE) {
            return Float.valueOf(0);
        }
        if (returnType == Double.TYPE) {
            return Double.valueOf(0);
        }
        if (returnType == Character.TYPE) {
            return Character.valueOf('\0');
        }
        if (returnType.isArray()) {
            return Array.newInstance(returnType.getComponentType(), 0);
        }
        if (Map.class.isAssignableFrom(returnType)) {
            return Collections.emptyMap();
        }
        if (List.class.isAssignableFrom(returnType)) {
            return Collections.emptyList();
        }
        if (Set.class.isAssignableFrom(returnType)) {
            return Collections.emptySet();
        }
        if (returnType.isInterface()
            && IInterface.class.isAssignableFrom(returnType)) {
            return createLocalInterface(returnType);
        }
        return null;
    }

    private static IInterface createLocalInterface(Class<?> type) {
        Binder binder = new Binder();
        IInterface owner = (IInterface)Proxy.newProxyInstance(
            type.getClassLoader(),
            new Class<?>[] { type },
            new LocalInterfaceHandler(binder, type.getName()));
        binder.attachInterface(owner, type.getName());
        return owner;
    }

    private static final class LocalInterfaceHandler implements InvocationHandler {
        private final IBinder binder;
        private final String descriptor;

        LocalInterfaceHandler(IBinder binder, String descriptor) {
            this.binder = binder;
            this.descriptor = descriptor == null ? "" : descriptor;
        }

        @Override
        public Object invoke(Object proxy, Method method, Object[] args)
            throws Throwable {
            if ("asBinder".equals(method.getName())) {
                return binder;
            }
            if ("android.view.IWindowManager".equals(descriptor)) {
                Object value = windowManagerValue(method, args);
                if (value != null) {
                    return value;
                }
            }
            if ("android.os.IUserManager".equals(descriptor)) {
                Object value = userManagerValue(method, args);
                if (value != null) {
                    return value;
                }
            }
            if ("android.content.pm.ILauncherApps".equals(descriptor)) {
                Object value = launcherAppsValue(method, args);
                if (value != null) {
                    return value;
                }
            }
            if ("android.app.IWallpaperManager".equals(descriptor)) {
                Object value = wallpaperManagerValue(method, args);
                if (value != null) {
                    return value;
                }
            }
            return defaultValue(method.getReturnType());
        }

        private Object wallpaperManagerValue(Method method, Object[] args) {
            String name = method.getName();
            if ("getProfileIds".equals(name) || "getProfileIdsWithDisabled".equals(name)) {
                return new int[] { 0 };
            }
            if ("isUserUnlocked".equals(name) || "isUserRunning".equals(name) ||
                "isWallpaperSupported".equals(name) || "isSetWallpaperAllowed".equals(name)) {
                return Boolean.TRUE;
            }
            if ("getUserSerialNumber".equals(name)) {
                return Long.valueOf(0L);
            }
            if ("getUserHandle".equals(name) || "getWallpaperId".equals(name)) {
                return Integer.valueOf(1);
            }
            if ("getWallpaperColors".equals(name) || "getWallpaperColorsWithFeature".equals(name)) {
                return createSyntheticWallpaperColors();
            }
            return null;
        }

        private Object createSyntheticWallpaperColors() {
            try {
                Class<?> colorsCls = Class.forName("android.app.WallpaperColors");
                Class<?> colorCls = Class.forName("android.graphics.Color");
                Method valueOf = colorCls.getMethod("valueOf", Integer.TYPE);
                Object primaryColor = valueOf.invoke(null, Integer.valueOf(0xFF1E1E1E));
                Constructor<?> ctor = colorsCls.getConstructor(colorCls, colorCls, colorCls);
                ctor.setAccessible(true);
                return ctor.newInstance(primaryColor, null, null);
            } catch (Throwable ignored) {
                return null;
            }
        }

        private Object windowManagerValue(Method method, Object[] args)
            throws Exception {
            String name = method.getName();
            if ("getPossibleDisplayInfo".equals(name)) {
                int displayId = args != null && args.length > 0
                    ? ((Integer)args[0]).intValue() : 0;
                if (displayId == 0) {
                    return Collections.singletonList(createDisplayInfo());
                }
                return Collections.emptyList();
            }
            if ("getWindowInsets".equals(name)) {
                return Boolean.TRUE;
            }
            return null;
        }

        private Object userManagerValue(Method method, Object[] args) {
            String name = method.getName();
            if ("getProfileIds".equals(name)
                || "getProfileIdsWithDisabled".equals(name)) {
                return new int[] { 0 };
            }
            if ("isUserUnlocked".equals(name)
                || "isUserRunning".equals(name)) {
                return Boolean.TRUE;
            }
            if ("getUserSerialNumber".equals(name)) {
                return Long.valueOf(0L);
            }
            if ("getUserHandle".equals(name)) {
                return Integer.valueOf(0);
            }
            return null;
        }

        private Object launcherAppsValue(Method method, Object[] args) {
            String name = method.getName();
            System.out.println("[Muplar/ART] launcherAppsValue enter name="
                + name);
            System.out.flush();
            try {
                if ("getActivityOverrides".equals(name)) {
                    return Collections.emptyMap();
                }
                if ("getAllSessions".equals(name)
                    || "getAllPackageInstallerSessions".equals(name)) {
                    return createParceledListSlice(Collections.emptyList());
                }
                if ("getUserProfiles".equals(name)) {
                    List<Object> profiles = new ArrayList<>();
                    Object user = buildUserHandle(0);
                    if (user != null) profiles.add(user);
                    return profiles;
                }
                if ("getLauncherActivities".equals(name)) {
                    String packageFilter = args != null && args.length > 1
                        && args[1] instanceof String
                        ? (String) args[1] : null;
                    return createParceledListSlice(
                        buildLauncherActivities(packageFilter));
                }
                if ("isActivityEnabled".equals(name)
                    || "isPackageEnabled".equals(name)) {
                    return Boolean.TRUE;
                }
            } catch (Throwable t) {
                System.err.println(
                    "[Muplar/ART] launcherAppsValue " + name + " failed: "
                    + t.getClass().getName() + ": " + t.getMessage());
                return defaultValue(method.getReturnType());
            }
            return null;
        }
    }

    private static Object createParceledListSlice(List<?> items) {
        try {
            Class<?> type = Class.forName("android.content.pm.ParceledListSlice");
            Constructor<?> ctor = type.getConstructor(List.class);
            return ctor.newInstance(items);
        } catch (Throwable ignored) {
            return null;
        }
    }

    private static List<Object> buildLauncherActivities(String packageFilter) {
        List<Object> result = new ArrayList<>();
        for (InstalledPackage pkg : queryInstalledPackages()) {
            if (packageFilter != null && !packageFilter.isEmpty()
                && !packageFilter.equals(pkg.packageName)) {
                continue;
            }
            try {
                Object info = buildLauncherActivityInfoInternal(pkg);
                if (info != null) result.add(info);
            } catch (Throwable t) {
                System.err.println(
                    "[Muplar/ART] launcher activity build failed package="
                    + pkg.packageName + " error=" + t.getClass().getName()
                    + ": " + t.getMessage());
            }
        }
        return result;
    }

    private static final class InstalledPackage {
        String packageName = "";
        String activity = "";
        String label = "";
        String apk = "";
    }

    /**
     * Reads the host-written package registry via muplard's QueryPackages
     * opcode. The host (PrefixManagerApp.mm) writes one record per
     * installed, non-launcher APK, "---"-separated, each a block of
     * "key=value" lines (package/activity/label/apk).
     */
    private static List<InstalledPackage> queryInstalledPackages() {
        List<InstalledPackage> result = new ArrayList<>();
        String text = FrameworkServiceClient.request("query-packages", "");
        if (text == null || text.isEmpty()) return result;
        InstalledPackage current = new InstalledPackage();
        boolean any = false;
        for (String line : text.split("\n", -1)) {
            if ("---".equals(line.trim())) {
                if (any && !current.packageName.isEmpty())
                    result.add(current);
                current = new InstalledPackage();
                any = false;
                continue;
            }
            if (line.startsWith("package=")) {
                current.packageName = line.substring(8);
                any = true;
            } else if (line.startsWith("activity=")) {
                current.activity = line.substring(9);
                any = true;
            } else if (line.startsWith("label=")) {
                current.label = line.substring(6);
                any = true;
            } else if (line.startsWith("apk=")) {
                current.apk = line.substring(4);
                any = true;
            }
        }
        if (any && !current.packageName.isEmpty())
            result.add(current);
        return result;
    }

    private static Object buildUserHandle(int uid) throws Exception {
        Class<?> type = Class.forName("android.os.UserHandle");
        Method of = type.getMethod("of", Integer.TYPE);
        return of.invoke(null, Integer.valueOf(uid));
    }

    private static Object buildApplicationInfo(InstalledPackage pkg)
        throws Exception {
        Class<?> type = Class.forName("android.content.pm.ApplicationInfo");
        Object info = type.getDeclaredConstructor().newInstance();
        setFieldIfPresent(info, "packageName", pkg.packageName);
        setFieldIfPresent(info, "processName", pkg.packageName);
        setFieldIfPresent(info, "sourceDir", pkg.apk);
        setFieldIfPresent(info, "publicSourceDir", pkg.apk);
        setFieldIfPresent(info, "uid", Integer.valueOf(10000));
        setFieldIfPresent(info, "targetSdkVersion", Integer.valueOf(35));
        setFieldIfPresent(info, "flags", Integer.valueOf(0));
        if (pkg.label != null && !pkg.label.isEmpty())
            setFieldIfPresent(info, "nonLocalizedLabel", pkg.label);
        return info;
    }

    private static Object buildLauncherActivityInfoInternal(
        InstalledPackage pkg) throws Exception {
        Object appInfo = buildApplicationInfo(pkg);

        Class<?> activityInfoType =
            Class.forName("android.content.pm.ActivityInfo");
        Object activityInfo = activityInfoType.getDeclaredConstructor()
            .newInstance();
        setFieldIfPresent(activityInfo, "packageName", pkg.packageName);
        setFieldIfPresent(activityInfo, "name", pkg.activity);
        setFieldIfPresent(activityInfo, "processName", pkg.packageName);
        setFieldIfPresent(activityInfo, "applicationInfo", appInfo);
        setFieldIfPresent(activityInfo, "enabled", Boolean.TRUE);
        setFieldIfPresent(activityInfo, "exported", Boolean.TRUE);
        if (pkg.label != null && !pkg.label.isEmpty())
            setFieldIfPresent(activityInfo, "nonLocalizedLabel", pkg.label);

        Class<?> incrementalType =
            Class.forName("android.content.pm.IncrementalStatesInfo");
        Constructor<?> incrementalCtor = incrementalType.getConstructor(
            Boolean.TYPE, Float.TYPE, Long.TYPE);
        Object incremental = incrementalCtor.newInstance(
            Boolean.FALSE, Float.valueOf(1.0f), Long.valueOf(0L));

        Object user = buildUserHandle(0);

        Class<?> internalType =
            Class.forName("android.content.pm.LauncherActivityInfoInternal");
        Constructor<?> ctor = internalType.getConstructor(
            activityInfoType, incrementalType,
            Class.forName("android.os.UserHandle"), Boolean.TYPE);
        return ctor.newInstance(activityInfo, incremental, user,
            Boolean.FALSE);
    }

    private static Object createDisplayInfo() throws Exception {
        Class<?> infoType = Class.forName("android.view.DisplayInfo");
        Object info = infoType.getDeclaredConstructor().newInstance();
        setFieldIfPresent(info, "displayId", Integer.valueOf(0));
        setFieldIfPresent(info, "displayGroupId", Integer.valueOf(0));
        setFieldIfPresent(info, "name", "Muplar Display");
        setFieldIfPresent(info, "uniqueId", "muplar:display:0");
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
        setFieldIfPresent(info, "logicalDensityDpi", Integer.valueOf(480));
        setFieldIfPresent(info, "renderFrameRate", Float.valueOf(60.0f));
        return info;
    }

    private static void setFieldIfPresent(Object target, String name, Object value) {
        Class<?> type = target.getClass();
        while (type != null) {
            try {
                Field field = type.getDeclaredField(name);
                field.setAccessible(true);
                field.set(target, value);
                return;
            } catch (NoSuchFieldException ignored) {
                type = type.getSuperclass();
            } catch (Throwable ignored) {
                return;
            }
        }
    }

    private static final class ServiceManagerHandler implements InvocationHandler {
        @Override
        public Object invoke(Object proxy, Method method, Object[] args)
            throws Throwable {
            String name = method.getName();
            Class<?> returnType = method.getReturnType();

            if ("addService".equals(name)) {
                if (args != null && args.length >= 2 && args[0] instanceof String
                    && args[1] instanceof IBinder) {
                    services.put((String)args[0], (IBinder)args[1]);
                }
                return null;
            }
            if ("getService".equals(name) || "checkService".equals(name)
                || "waitForService".equals(name)) {
                return getBinder(args != null && args.length > 0
                    ? String.valueOf(args[0]) : "");
            }
            if ("getService2".equals(name) || "checkService2".equals(name)) {
                return createService(
                    getBinder(args != null && args.length > 0
                        ? String.valueOf(args[0]) : ""));
            }
            if ("listServices".equals(name) || "getDeclaredInstances".equals(name)
                || "getUpdatableNames".equals(name)) {
                return Array.newInstance(returnType.getComponentType(), 0);
            }
            if ("isDeclared".equals(name)) {
                return Boolean.FALSE;
            }
            if ("updatableViaApex".equals(name)) {
                return null;
            }
            if ("asBinder".equals(name)) {
                return getBinder("servicemanager");
            }
            return defaultValue(returnType);
        }

        private Object createService(IBinder binder) throws Exception {
            Class<?> metadataClass = Class.forName("android.os.ServiceWithMetadata");
            Object metadata = metadataClass.getDeclaredConstructor().newInstance();
            Field serviceField = metadataClass.getField("service");
            serviceField.set(metadata, binder);
            Field lazyField = metadataClass.getField("isLazyService");
            lazyField.setBoolean(metadata, false);

            Class<?> serviceClass = Class.forName("android.os.Service");
            Method wrap = serviceClass.getMethod("serviceWithMetadata", metadataClass);
            return wrap.invoke(null, metadata);
        }
    }

    private static final class NoNativeBinder implements IBinder {
        private final String descriptor;

        NoNativeBinder(String descriptor) {
            this.descriptor = descriptor == null ? "" : descriptor;
        }

        @Override
        public String getInterfaceDescriptor() {
            return descriptor;
        }

        @Override
        public boolean pingBinder() {
            return true;
        }

        @Override
        public boolean isBinderAlive() {
            return true;
        }

        @Override
        public IInterface queryLocalInterface(String descriptor) {
            return null;
        }

        @Override
        public void dump(FileDescriptor fd, String[] args) {
        }

        @Override
        public void dumpAsync(FileDescriptor fd, String[] args) {
        }

        @Override
        public boolean transact(int code, Parcel data, Parcel reply, int flags)
            throws RemoteException {
            if (reply != null) {
                try {
                    reply.writeNoException();
                } catch (Throwable ignored) {
                }
            }
            return true;
        }

        @Override
        public void linkToDeath(DeathRecipient recipient, int flags) {
        }

        @Override
        public boolean unlinkToDeath(DeathRecipient recipient, int flags) {
            return true;
        }

        @Override
        public void addFrozenStateChangeCallback(
            Executor executor,
            FrozenStateChangeCallback callback) {
        }

        @Override
        public boolean removeFrozenStateChangeCallback(
            FrozenStateChangeCallback callback) {
            return true;
        }
    }
}
