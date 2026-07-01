package android.text;

import android.graphics.Canvas;

public abstract class Layout {
    public enum Alignment { ALIGN_NORMAL, ALIGN_OPPOSITE, ALIGN_CENTER }

    protected final CharSequence text;
    protected final TextPaint paint;
    protected final int width;

    protected Layout(CharSequence text, TextPaint paint, int width) {
        this.text = text;
        this.paint = paint;
        this.width = Math.max(0, width);
    }

    public int getWidth() { return width; }
    public int getHeight() {
        return Math.max(1, Math.round(paint == null ? 16.0f : paint.getTextSize()));
    }
    public void draw(Canvas canvas) {}
}
