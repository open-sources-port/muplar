package android.appwidget;

import android.content.Context;
import android.os.Looper;
import android.widget.RemoteViews;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

public class AppWidgetHost {
    private static final AtomicInteger NEXT_ID = new AtomicInteger(1);
    private static final Set<Integer> IDS =
        java.util.Collections.newSetFromMap(
            new ConcurrentHashMap<Integer, Boolean>());
    protected final Context context;
    public AppWidgetHost(Context context, int hostId) { this.context = context; }
    public AppWidgetHost(Context context, int hostId,
            RemoteViews.InteractionHandler interactionHandler, Looper looper) {
        this.context = context;
    }
    public void startListening() {}
    public void stopListening() {}
    public int allocateAppWidgetId() {
        int id = NEXT_ID.getAndIncrement(); IDS.add(id); return id;
    }
    public int[] getAppWidgetIds() {
        int[] result = new int[IDS.size()]; int index = 0;
        for (Integer id : IDS) result[index++] = id;
        return result;
    }
    public void deleteAppWidgetId(int appWidgetId) {
        IDS.remove(appWidgetId); AppWidgetManager.deleteBinding(appWidgetId);
    }
    public void deleteHost() {
        for (Integer id : new java.util.ArrayList<Integer>(IDS))
            deleteAppWidgetId(id);
    }
    public void clearViews() {}
    public AppWidgetHostView createView(Context context, int appWidgetId,
            AppWidgetProviderInfo info) {
        AppWidgetHostView view = onCreateView(context, appWidgetId, info);
        view.setAppWidget(appWidgetId, info);
        return view;
    }
    protected AppWidgetHostView onCreateView(Context context, int appWidgetId,
            AppWidgetProviderInfo info) {
        return new AppWidgetHostView(context);
    }
    protected void onProviderChanged(int appWidgetId, AppWidgetProviderInfo info) {}
    protected void onProvidersChanged() {}
    public void onAppWidgetRemoved(int appWidgetId) {}
}
