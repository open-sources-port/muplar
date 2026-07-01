package android.database;

import android.net.Uri;
import android.os.Handler;

public abstract class ContentObserver {
    private final Handler handler;
    public ContentObserver(Handler handler) { this.handler = handler; }
    public boolean deliverSelfNotifications() { return false; }
    public void onChange(boolean selfChange) {}
    public void onChange(boolean selfChange, Uri uri) { onChange(selfChange); }
    public final void dispatchChange(final boolean selfChange, final Uri uri) {
        Runnable action = new Runnable() {
            @Override public void run() { onChange(selfChange, uri); }
        };
        if (handler == null) action.run(); else handler.post(action);
    }
}
