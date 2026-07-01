package android.app;

import android.graphics.Rect;

public class WindowConfiguration {
    private final Rect bounds = new Rect();
    private final Rect maxBounds = new Rect();
    private int rotation;
    public Rect getBounds() { return bounds; }
    public void setBounds(Rect value) {
        if (value == null) bounds.set(0, 0, 0, 0);
        else bounds.set(value.left, value.top, value.right, value.bottom);
    }
    public Rect getMaxBounds() { return maxBounds; }
    public int getRotation() { return rotation; }
    public void setRotation(int value) { rotation = value; }
    public int getWindowingMode() { return 1; }
    public void setTo(WindowConfiguration source) {
        setBounds(source.bounds);
        maxBounds.set(source.maxBounds.left, source.maxBounds.top,
            source.maxBounds.right, source.maxBounds.bottom);
        rotation = source.rotation;
    }
}
