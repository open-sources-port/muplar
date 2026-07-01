package android.view;

import android.graphics.Rect;

public final class WindowMetrics {
    private final Rect bounds;
    private final WindowInsets insets;
    private final float density;
    public WindowMetrics(Rect bounds, WindowInsets insets) {
        this(bounds, insets, 1.0f);
    }
    public WindowMetrics(Rect bounds, WindowInsets insets, float density) {
        this.bounds = bounds;
        this.insets = insets;
        this.density = density;
    }
    public Rect getBounds() {
        return new Rect(bounds.left, bounds.top, bounds.right, bounds.bottom);
    }
    public WindowInsets getWindowInsets() { return insets; }
    public float getDensity() { return density; }
}
