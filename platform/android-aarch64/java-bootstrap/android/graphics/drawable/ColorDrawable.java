package android.graphics.drawable;

public class ColorDrawable extends Drawable {
    private int color;

    public ColorDrawable() {}
    public ColorDrawable(int color) { this.color = color; }
    public int getColor() { return color; }
    public void setColor(int color) { this.color = color; invalidateSelf(); }
}
