package android.hardware.display;

import android.os.Handler;
import android.view.Display;
import java.util.concurrent.CopyOnWriteArrayList;

public final class DisplayManager {
    public static final String DISPLAY_CATEGORY_PRESENTATION =
        "android.hardware.display.category.PRESENTATION";
    public interface DisplayListener {
        void onDisplayAdded(int displayId);
        void onDisplayRemoved(int displayId);
        void onDisplayChanged(int displayId);
    }
    private final Display display;
    private final CopyOnWriteArrayList<DisplayListener> listeners =
        new CopyOnWriteArrayList<>();
    public DisplayManager(Display display) { this.display = display; }
    public Display getDisplay(int displayId) {
        return displayId == Display.DEFAULT_DISPLAY ? display : null;
    }
    public Display[] getDisplays() { return new Display[] {display}; }
    public Display[] getDisplays(String category) { return getDisplays(); }
    public void registerDisplayListener(DisplayListener listener, Handler handler) {
        if (listener != null) listeners.addIfAbsent(listener);
    }
    public void unregisterDisplayListener(DisplayListener listener) {
        listeners.remove(listener);
    }
}
