package android.content;

import android.database.ContentObserver;
import android.net.Uri;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

public class ContentResolver {
    private final Map<ContentObserver, Uri> observers =
        new ConcurrentHashMap<ContentObserver, Uri>();
    private final Context context;
    public ContentResolver(Context context) { this.context = context; }
    public Context getContext() { return context; }
    public void registerContentObserver(Uri uri, boolean notifyForDescendants,
            ContentObserver observer) {
        if (observer != null) observers.put(observer, uri);
    }
    public void unregisterContentObserver(ContentObserver observer) {
        observers.remove(observer);
    }
    public void notifyChange(Uri uri, ContentObserver observer) {
        for (Map.Entry<ContentObserver, Uri> entry : observers.entrySet()) {
            if (entry.getKey() != observer &&
                (entry.getValue() == null || entry.getValue().equals(uri)))
                entry.getKey().dispatchChange(false, uri);
        }
    }
}
