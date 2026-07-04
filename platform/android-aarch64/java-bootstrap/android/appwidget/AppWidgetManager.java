package android.appwidget;

import android.content.ComponentName;
import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.os.UserHandle;
import android.widget.RemoteViews;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

public class AppWidgetManager {
    public static final String ACTION_APPWIDGET_UPDATE =
        "android.appwidget.action.APPWIDGET_UPDATE";
    public static final String EXTRA_APPWIDGET_ID = "appWidgetId";
    public static final int INVALID_APPWIDGET_ID = 0;
    private static final Map<Integer, AppWidgetProviderInfo> BINDINGS =
        new ConcurrentHashMap<Integer, AppWidgetProviderInfo>();
    private static final Map<Integer, RemoteViews> VIEWS =
        new ConcurrentHashMap<Integer, RemoteViews>();
    private final PackageManager packageManager;

    private AppWidgetManager(Context context) {
        packageManager = context == null ? new PackageManager()
            : context.getPackageManager();
    }
    public static AppWidgetManager getInstance(Context context) {
        return new AppWidgetManager(context);
    }
    public List<AppWidgetProviderInfo> getInstalledProviders() {
        return providers(UserHandle.CURRENT);
    }
    public List<AppWidgetProviderInfo> getInstalledProvidersForProfile(
            UserHandle profile) {
        return providers(profile);
    }
    public List<AppWidgetProviderInfo> getInstalledProvidersForPackage(
            String packageName, UserHandle profile) {
        List<AppWidgetProviderInfo> result = new ArrayList<AppWidgetProviderInfo>();
        for (AppWidgetProviderInfo info : providers(profile))
            if (info.provider.getPackageName().equals(packageName)) result.add(info);
        return result;
    }
    private List<AppWidgetProviderInfo> providers(UserHandle profile) {
        List<AppWidgetProviderInfo> result = new ArrayList<AppWidgetProviderInfo>();
        for (ApplicationInfo app : packageManager.getInstalledApplications(0)) {
            if (app.widgetProvider == null || app.widgetProvider.isEmpty()) continue;
            AppWidgetProviderInfo info = new AppWidgetProviderInfo();
            info.provider = new ComponentName(app.packageName, app.widgetProvider);
            info.label = app.loadLabel(packageManager).toString();
            info.minWidth = 120;
            info.minHeight = 120;
            info.minResizeWidth = 60;
            info.minResizeHeight = 60;
            info.targetCellWidth = 2;
            info.targetCellHeight = 2;
            info.profile = profile == null ? UserHandle.CURRENT : profile;
            result.add(info);
        }
        return Collections.unmodifiableList(result);
    }
    public boolean bindAppWidgetIdIfAllowed(int id, ComponentName provider) {
        return bindAppWidgetIdIfAllowed(id, UserHandle.CURRENT, provider, null);
    }
    public boolean bindAppWidgetIdIfAllowed(int id, ComponentName provider,
            Bundle options) {
        return bindAppWidgetIdIfAllowed(id, UserHandle.CURRENT, provider, options);
    }
    public boolean bindAppWidgetIdIfAllowed(int id, UserHandle profile,
            ComponentName provider, Bundle options) {
        for (AppWidgetProviderInfo info : providers(profile)) {
            if (info.provider.equals(provider)) {
                BINDINGS.put(id, info);
                return true;
            }
        }
        return false;
    }
    public AppWidgetProviderInfo getAppWidgetInfo(int id) { return BINDINGS.get(id); }
    public int[] getAppWidgetIds(ComponentName provider) {
        List<Integer> result = new ArrayList<Integer>();
        for (Map.Entry<Integer, AppWidgetProviderInfo> entry : BINDINGS.entrySet())
            if (entry.getValue().provider.equals(provider)) result.add(entry.getKey());
        int[] ids = new int[result.size()];
        for (int i = 0; i < ids.length; i++) ids[i] = result.get(i);
        return ids;
    }
    public void updateAppWidget(int id, RemoteViews views) {
        if (views == null) VIEWS.remove(id); else VIEWS.put(id, views);
    }
    public void updateAppWidget(int[] ids, RemoteViews views) {
        if (ids != null) for (int id : ids) updateAppWidget(id, views);
    }
    public void notifyAppWidgetViewDataChanged(int[] ids, int viewId) {}
    public void updateAppWidgetOptions(int id, Bundle options) {}
    public Bundle getAppWidgetOptions(int id) { return new Bundle(); }
    static void deleteBinding(int id) { BINDINGS.remove(id); VIEWS.remove(id); }
    static RemoteViews viewsFor(int id) { return VIEWS.get(id); }
}
