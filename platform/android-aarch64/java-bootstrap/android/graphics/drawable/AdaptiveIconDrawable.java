package android.graphics.drawable;

import android.graphics.Path;

public class AdaptiveIconDrawable extends Drawable {
    private final Drawable background;
    private final Drawable foreground;

    public AdaptiveIconDrawable(Drawable background, Drawable foreground) {
        this.background = background;
        this.foreground = foreground;
    }
    public Drawable getBackground() { return background; }
    public Drawable getForeground() { return foreground; }
    public Path getIconMask() { return new Path(); }
    public static float getExtraInsetFraction() { return 0.0f; }
    @Override public void setBounds(int left, int top, int right, int bottom) {
        super.setBounds(left, top, right, bottom);
        if (background != null) background.setBounds(left, top, right, bottom);
        if (foreground != null) foreground.setBounds(left, top, right, bottom);
    }
}
