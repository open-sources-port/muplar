package android.graphics.drawable;

import android.graphics.Rect;
import android.graphics.Canvas;
import android.content.res.ColorStateList;

public class Drawable {
    public interface Callback {
        void invalidateDrawable(Drawable drawable);
        void scheduleDrawable(Drawable drawable, Runnable action, long when);
        void unscheduleDrawable(Drawable drawable, Runnable action);
    }
    private final String sourcePath;
    private Callback callback;
    private final Rect bounds = new Rect();
    private int alpha = 255;

    public Drawable() { this(""); }
    public Drawable(String sourcePath) {
        this.sourcePath = sourcePath;
    }

    public String getSourcePath() {
        return sourcePath;
    }
    public void setCallback(Callback callback) { this.callback = callback; }
    public Callback getCallback() { return callback; }
    public void setBounds(int left, int top, int right, int bottom) {
        bounds.set(left, top, right, bottom);
    }
    public void setBounds(Rect value) { bounds.set(value); }
    public Rect getBounds() { return new Rect(bounds); }
    public void invalidateSelf() {
        if (callback != null) callback.invalidateDrawable(this);
    }
    public void setAlpha(int alpha) { this.alpha = alpha; invalidateSelf(); }
    public int getAlpha() { return alpha; }
    public Drawable mutate() { return this; }
    public void setTint(int color) {}
    public void setTintList(ColorStateList colors) {}
    public void draw(Canvas canvas) {}
    public int getIntrinsicWidth() { return -1; }
    public int getIntrinsicHeight() { return -1; }
}
