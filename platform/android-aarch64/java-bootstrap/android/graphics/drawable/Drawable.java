package android.graphics.drawable;

import android.graphics.Rect;
import android.graphics.Canvas;
import android.content.res.ColorStateList;

public class Drawable {
    public abstract static class ConstantState {
        public abstract Drawable newDrawable();
        public int getChangingConfigurations() { return 0; }
    }
    public interface Callback {
        void invalidateDrawable(Drawable drawable);
        void scheduleDrawable(Drawable drawable, Runnable action, long when);
        void unscheduleDrawable(Drawable drawable, Runnable action);
    }
    private final String sourcePath;
    private Callback callback;
    private final Rect bounds = new Rect();
    private int alpha = 255;
    private boolean visible = true;
    private int[] state = new int[0];

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
    public boolean setVisible(boolean visible, boolean restart) {
        boolean changed = this.visible != visible;
        this.visible = visible;
        if (changed) invalidateSelf();
        return changed;
    }
    public boolean isVisible() { return visible; }
    public boolean setState(int[] value) {
        state = value == null ? new int[0] : value.clone();
        invalidateSelf();
        return true;
    }
    public int[] getState() { return state.clone(); }
    public boolean isStateful() { return false; }
    public Drawable mutate() { return this; }
    public ConstantState getConstantState() {
        final String path = sourcePath;
        return new ConstantState() {
            @Override public Drawable newDrawable() { return new Drawable(path); }
        };
    }
    public void setTint(int color) {}
    public void setTintList(ColorStateList colors) {}
    public void draw(Canvas canvas) {}
    public int getIntrinsicWidth() { return -1; }
    public int getIntrinsicHeight() { return -1; }
}
