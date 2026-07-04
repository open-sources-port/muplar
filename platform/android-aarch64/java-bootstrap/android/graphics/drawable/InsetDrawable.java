package android.graphics.drawable;

public class InsetDrawable extends DrawableWrapper {
    public InsetDrawable(Drawable drawable, float left, float top,
            float right, float bottom) {
        super(drawable);
    }
    public InsetDrawable(Drawable drawable, int inset) { super(drawable); }
}
