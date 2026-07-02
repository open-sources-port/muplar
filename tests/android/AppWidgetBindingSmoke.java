import android.appwidget.AppWidgetManager;
import android.appwidget.AppWidgetProviderInfo;
import android.content.ComponentName;
import android.os.UserHandle;
import java.io.File;
import java.io.FileOutputStream;
import java.util.List;
import java.util.Properties;

public final class AppWidgetBindingSmoke {
    public static void main(String[] args) throws Exception {
        if (args.length != 1) throw new AssertionError("registry path required");
        File registry = new File(args[0]);
        Properties values = new Properties();
        values.setProperty("count", "1");
        values.setProperty("package.0.name", "com.example.widget");
        values.setProperty("package.0.label", "Example Widget");
        values.setProperty("package.0.activity", "");
        values.setProperty("package.0.apk", "/tmp/example-widget.apk");
        values.setProperty("package.0.icon", "");
        values.setProperty("package.0.widget", "com.example.widget.ClockProvider");
        try (FileOutputStream output = new FileOutputStream(registry)) {
            values.store(output, "widget smoke");
        }
        System.setProperty("muplar.package.registry", registry.getAbsolutePath());
        System.setProperty("muplar.service.executable", "");
        System.setProperty("muplar.service.socket", "");

        AppWidgetManager manager = AppWidgetManager.getInstance(null);
        List<AppWidgetProviderInfo> providers =
            manager.getInstalledProvidersForProfile(UserHandle.CURRENT);
        if (providers.size() != 1)
            throw new AssertionError("expected one widget provider, got " +
                providers.size());
        AppWidgetProviderInfo info = providers.get(0);
        ComponentName expected = new ComponentName("com.example.widget",
            "com.example.widget.ClockProvider");
        if (!expected.equals(info.provider) || info.targetCellWidth != 2 ||
                info.targetCellHeight != 2)
            throw new AssertionError("provider metadata was not populated");
        if (!manager.bindAppWidgetIdIfAllowed(41, UserHandle.CURRENT,
                expected, null) || manager.getAppWidgetInfo(41) == null)
            throw new AssertionError("widget binding failed");
        if (manager.getAppWidgetIds(expected).length != 1)
            throw new AssertionError("bound widget id was not indexed");
        System.out.println("appWidgetBinding=ok provider=" +
            info.provider.flattenToString());
    }
}
