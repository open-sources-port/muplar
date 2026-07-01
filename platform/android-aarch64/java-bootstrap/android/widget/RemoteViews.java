package android.widget;

import android.app.PendingIntent;
import android.view.View;

public class RemoteViews {
    public interface InteractionHandler {
        boolean onInteraction(View view, PendingIntent pendingIntent,
            RemoteResponse response);
    }
    public static class RemoteResponse {}
    private final String packageName;
    private final int layoutId;
    public RemoteViews(String packageName, int layoutId) {
        this.packageName = packageName;
        this.layoutId = layoutId;
    }
    public String getPackage() { return packageName; }
    public int getLayoutId() { return layoutId; }
}
