package android.app;

import android.content.Context;

public class Application extends Context {
    public Application() {
        setProcessApplication(this);
    }
    public void onCreate() {}
    public void onTerminate() {}
    public void onLowMemory() {}
}
