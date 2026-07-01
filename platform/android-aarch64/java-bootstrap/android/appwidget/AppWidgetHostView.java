package android.appwidget;

import android.content.Context;
import android.graphics.Rect;
import android.widget.FrameLayout;
import android.widget.RemoteViews;
import java.util.concurrent.Executor;

public class AppWidgetHostView extends FrameLayout {
    private int appWidgetId;
    private AppWidgetProviderInfo appWidgetInfo;
    public AppWidgetHostView(Context context) { super(context); }
    public AppWidgetHostView(Context context, int animationIn, int animationOut) {
        super(context);
    }
    public void setAppWidget(int id, AppWidgetProviderInfo info) {
        appWidgetId = id;
        appWidgetInfo = info;
    }
    public int getAppWidgetId() { return appWidgetId; }
    public AppWidgetProviderInfo getAppWidgetInfo() { return appWidgetInfo; }
    public void updateAppWidget(RemoteViews views) {}
    public void setExecutor(Executor executor) {}
    public static Rect getDefaultPaddingForWidget(Context context,
            android.content.ComponentName component, Rect padding) {
        Rect result = padding == null ? new Rect() : padding;
        result.setEmpty();
        return result;
    }
}
