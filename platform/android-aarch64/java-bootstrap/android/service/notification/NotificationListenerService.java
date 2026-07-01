package android.service.notification;

import android.content.ComponentName;

public abstract class NotificationListenerService {
    public static void requestRebind(ComponentName componentName) {}
    public final void requestUnbind() {}
    public void onListenerConnected() {}
    public void onListenerDisconnected() {}
}
