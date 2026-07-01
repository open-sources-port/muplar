package android.view;

import android.os.Binder;
import android.os.IBinder;

public final class WindowManagerGlobal {
    private static final IWindowManager SERVICE = new IWindowManager() {
        private final Binder binder = new Binder();
        public void registerSystemGestureExclusionListener(
                ISystemGestureExclusionListener listener, int displayId) {}
        public void unregisterSystemGestureExclusionListener(
                ISystemGestureExclusionListener listener, int displayId) {}
        public IBinder asBinder() { return binder; }
    };
    private WindowManagerGlobal() {}
    public static IWindowManager getWindowManagerService() { return SERVICE; }
}
