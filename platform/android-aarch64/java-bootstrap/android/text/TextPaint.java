package android.text;

import android.graphics.Paint;

public class TextPaint extends Paint {
    public int linkColor;
    public float density = 1.0f;

    public TextPaint() {}
    public TextPaint(int flags) { super(flags); }
    public TextPaint(Paint paint) { super(paint); }
}
