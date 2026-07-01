package android.view.accessibility;

import android.content.Context;

public final class AccessibilityManager {
    public interface AccessibilityStateChangeListener {
        void onAccessibilityStateChanged(boolean enabled);
    }
    public interface TouchExplorationStateChangeListener {
        void onTouchExplorationStateChanged(boolean enabled);
    }

    private static final AccessibilityManager INSTANCE = new AccessibilityManager();
    private AccessibilityManager() {}
    public static AccessibilityManager getInstance(Context context) { return INSTANCE; }
    public boolean isEnabled() { return false; }
    public boolean isTouchExplorationEnabled() { return false; }
    public boolean addAccessibilityStateChangeListener(
            AccessibilityStateChangeListener listener) { return true; }
    public boolean removeAccessibilityStateChangeListener(
            AccessibilityStateChangeListener listener) { return true; }
    public boolean addTouchExplorationStateChangeListener(
            TouchExplorationStateChangeListener listener) { return true; }
    public boolean removeTouchExplorationStateChangeListener(
            TouchExplorationStateChangeListener listener) { return true; }
}
