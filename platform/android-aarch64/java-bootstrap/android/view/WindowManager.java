package android.view;

import java.util.Set;

public interface WindowManager {
    Display getDefaultDisplay();
    WindowMetrics getCurrentWindowMetrics();
    WindowMetrics getMaximumWindowMetrics();
    Set<WindowMetrics> getPossibleMaximumWindowMetrics(int displayId);
}
