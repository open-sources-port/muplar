package android.graphics.drawable;

import android.graphics.Paint;
import android.graphics.drawable.shapes.Shape;

public class ShapeDrawable extends Drawable {
    private Shape shape;
    private final Paint paint = new Paint();
    public ShapeDrawable() {}
    public ShapeDrawable(Shape shape) { this.shape = shape; }
    public Shape getShape() { return shape; }
    public void setShape(Shape value) { shape = value; }
    public Paint getPaint() { return paint; }
}
