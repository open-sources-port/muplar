package android.content;

import android.content.pm.PackageManager;
import android.content.res.Resources;
import android.content.pm.LauncherApps;
import android.content.pm.ShortcutManager;
import android.appwidget.AppWidgetManager;
import android.app.ActivityManager;
import android.view.WindowManager;
import android.view.inputmethod.InputMethodManager;
import android.os.UserManager;

public abstract class Context {
    public static final String LAUNCHER_APPS_SERVICE = "launcherapps";
    public static final String SHORTCUT_SERVICE = "shortcut";
    public static final String APPWIDGET_SERVICE = "appwidget";
    public static final String ACTIVITY_SERVICE = "activity";
    public static final String WINDOW_SERVICE = "window";
    public static final String INPUT_METHOD_SERVICE = "input_method";
    public static final String USER_SERVICE = "user";
    private static final PackageManager pmInstance = new PackageManager();
    private static final Resources resourcesInstance = new Resources();
    private static final LauncherApps launcherAppsInstance =
        new LauncherApps(pmInstance);
    private static final ShortcutManager shortcutManagerInstance =
        new ShortcutManager();
    private static final ActivityManager activityManagerInstance =
        new ActivityManager();
    private static final WindowManager windowManagerInstance = new WindowManager();
    private static final InputMethodManager inputMethodManagerInstance =
        new InputMethodManager();
    private static final UserManager userManagerInstance = new UserManager();
    private final ContentResolver contentResolver = new ContentResolver(this);
    private final Resources.Theme theme = resourcesInstance.newTheme();

    public PackageManager getPackageManager() {
        return pmInstance;
    }

    public Resources getResources() {
        return resourcesInstance;
    }

    public String getString(int id) {
        return resourcesInstance.getString(id);
    }

    public Resources.Theme getTheme() { return theme; }

    public Object getSystemService(String name) {
        if ("launcherapps".equals(name)) return launcherAppsInstance;
        if ("shortcut".equals(name)) return shortcutManagerInstance;
        if ("appwidget".equals(name)) return AppWidgetManager.getInstance(this);
        if ("activity".equals(name)) return activityManagerInstance;
        if ("window".equals(name)) return windowManagerInstance;
        if ("input_method".equals(name)) return inputMethodManagerInstance;
        if ("user".equals(name)) return userManagerInstance;
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
        if (serviceClass == UserManager.class)
            return serviceClass.cast(userManagerInstance);
        return null;
    }

    public ContentResolver getContentResolver() { return contentResolver; }
    public int checkSelfPermission(String permission) {
        return PackageManager.PERMISSION_GRANTED;
    }
}
