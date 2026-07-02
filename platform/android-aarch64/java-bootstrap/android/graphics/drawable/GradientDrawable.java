package android.graphics.drawable;

import android.content.res.ColorStateList;

public class GradientDrawable extends Drawable {
    public static final int RECTANGLE = 0;
    public static final int OVAL = 1;
    private int color;
    private float cornerRadius;
    private int shape = RECTANGLE;
    public GradientDrawable() {}
    public void setColor(int value) { color = value; }
    public void setColor(ColorStateList value) {
        color = value == null ? 0 : value.getDefaultColor();
    }
    public int getColor() { return color; }
    public void setCornerRadius(float value) { cornerRadius = value; }
    public void setCornerRadii(float[] values) {
        if (values != null && values.length > 0) cornerRadius = values[0];
    }
    public float getCornerRadius() { return cornerRadius; }
    public void setShape(int value) { shape = value; }
    public int getShape() { return shape; }
    public void setStroke(int width, int color) {}
    public void setSize(int width, int height) {}
}
