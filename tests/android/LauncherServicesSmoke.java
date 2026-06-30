import android.app.Activity;
import android.appwidget.AppWidgetManager;
import android.content.pm.LauncherActivityInfo;
import android.content.pm.LauncherApps;
import android.content.pm.ShortcutManager;
import android.os.UserHandle;
import java.util.List;

public final class LauncherServicesSmoke {
    public static void main(String[] args) {
        Activity context = new Activity();
        LauncherApps launcherApps = context.getSystemService(LauncherApps.class);
        List<LauncherActivityInfo> activities =
            launcherApps.getActivityList(null, UserHandle.CURRENT);
        if (activities.isEmpty())
            throw new AssertionError("LauncherApps returned no activities");
        ShortcutManager shortcuts =
            context.getSystemService(ShortcutManager.class);
        if (shortcuts.isRequestPinShortcutSupported())
            throw new AssertionError("pin shortcuts should be disabled");
        AppWidgetManager widgets =
            context.getSystemService(AppWidgetManager.class);
        if (!widgets.getInstalledProviders().isEmpty())
            throw new AssertionError("widget baseline should be empty");
        System.out.println("launcherActivities=" + activities.size());
        System.out.println("first=" +
            activities.get(0).getComponentName().flattenToString());
        System.out.println("shortcutBaseline=ok");
        System.out.println("widgetBaseline=ok");
    }
}
