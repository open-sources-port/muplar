package android.text.style;

import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.drawable.Drawable;

public abstract class DynamicDrawableSpan {
    public static final int ALIGN_BOTTOM = 0;
    public static final int ALIGN_BASELINE = 1;
    public static final int ALIGN_CENTER = 2;
    private final int verticalAlignment;

    protected DynamicDrawableSpan() { this(ALIGN_BOTTOM); }
    protected DynamicDrawableSpan(int verticalAlignment) {
        this.verticalAlignment = verticalAlignment;
    }
    public abstract Drawable getDrawable();
    public int getSize(Paint paint, CharSequence text, int start, int end,
            Paint.FontMetricsInt metrics) {
        Rect bounds = getDrawable().getBounds();
        return Math.max(0, bounds.right - bounds.left);
    }
    public void draw(Canvas canvas, CharSequence text, int start, int end,
            float x, int top, int y, int bottom, Paint paint) {
        Drawable drawable = getDrawable();
        canvas.save();
        canvas.translate(x, bottom - drawable.getBounds().bottom);
        drawable.draw(canvas);
        canvas.restore();
    }
    public int getVerticalAlignment() { return verticalAlignment; }
}
