package android.app;

import android.content.Context;
import android.content.Intent;
import android.os.IBinder;

public abstract class Service extends Context {
    public void onCreate() {}
    public void onDestroy() {}
    public abstract IBinder onBind(Intent intent);
    public boolean onUnbind(Intent intent) { return false; }
    public void stopSelf() {}
    public Application getApplication() {
        Context context = getApplicationContext();
        return context instanceof Application ? (Application) context : null;
    }
}
