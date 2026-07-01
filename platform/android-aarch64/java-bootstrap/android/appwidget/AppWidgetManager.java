package android.appwidget;

import android.content.Context;
import java.util.Collections;
import java.util.List;

public class AppWidgetManager {
    public static final String ACTION_APPWIDGET_UPDATE =
        "android.appwidget.action.APPWIDGET_UPDATE";
    private static final AppWidgetManager INSTANCE = new AppWidgetManager();
    public static AppWidgetManager getInstance(Context context) { return INSTANCE; }
    public List<AppWidgetProviderInfo> getInstalledProviders() {
        return Collections.emptyList();
    }
}
