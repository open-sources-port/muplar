package android.widget;

import android.app.PendingIntent;
import android.view.View;
import android.view.ViewGroup;
import android.content.Context;

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
    public View apply(Context context, ViewGroup parent) {
        if (layoutId != 0)
            return android.view.LayoutInflater.from(context).inflate(
                layoutId, parent, false);
        TextView placeholder = new TextView(context);
        placeholder.setText(packageName);
        return placeholder;
    }
    public void reapply(Context context, View view) {}
}
