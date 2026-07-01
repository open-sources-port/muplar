package android.app;

import android.content.Context;

public class Application extends Context {
    private static volatile Application current;
    public Application() {
        current = this;
        setProcessApplication(this);
    }
    public static Application getCurrentApplication() { return current; }
    public void onCreate() {}
    public void onTerminate() {}
    public void onLowMemory() {}
}
