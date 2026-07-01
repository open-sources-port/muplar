package android.view;

import android.graphics.Rect;
import android.util.DisplayMetrics;
import java.util.Collections;
import java.util.Set;

public final class SimpleWindowManager implements WindowManager {
    private final Display display = new Display();
    public Display getDefaultDisplay() { return display; }
    public WindowMetrics getCurrentWindowMetrics() { return metrics(); }
    public WindowMetrics getMaximumWindowMetrics() { return metrics(); }
    public Set<WindowMetrics> getPossibleMaximumWindowMetrics(int displayId) {
        return displayId == Display.DEFAULT_DISPLAY
            ? Collections.singleton(metrics()) : Collections.emptySet();
    }
    private WindowMetrics metrics() {
        DisplayMetrics metrics = new DisplayMetrics();
        display.getMetrics(metrics);
        return new WindowMetrics(new Rect(0, 0, metrics.widthPixels,
            metrics.heightPixels), WindowInsets.CONSUMED, metrics.density);
    }
}
