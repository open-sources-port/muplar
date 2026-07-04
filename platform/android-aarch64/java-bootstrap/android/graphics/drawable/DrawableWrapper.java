package android.graphics.drawable;

import android.graphics.Canvas;

public class DrawableWrapper extends Drawable {
    private Drawable drawable;
    public DrawableWrapper(Drawable drawable) { this.drawable = drawable; }
    public Drawable getDrawable() { return drawable; }
    public void setDrawable(Drawable value) { drawable = value; invalidateSelf(); }
    @Override public void draw(Canvas canvas) {
        if (drawable != null) drawable.draw(canvas);
    }
    @Override public void setAlpha(int alpha) {
        super.setAlpha(alpha);
        if (drawable != null) drawable.setAlpha(alpha);
    }
}
