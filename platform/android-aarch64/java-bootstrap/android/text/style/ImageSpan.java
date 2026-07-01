package android.text.style;

import android.content.Context;
import android.graphics.drawable.Drawable;

public class ImageSpan extends DynamicDrawableSpan {
    private final Drawable drawable;

    public ImageSpan(Drawable drawable) { this(drawable, ALIGN_BOTTOM); }
    public ImageSpan(Drawable drawable, int verticalAlignment) {
        super(verticalAlignment);
        this.drawable = drawable;
    }
    public ImageSpan(Context context, int resourceId) {
        this(context.getDrawable(resourceId));
    }
    public ImageSpan(Context context, int resourceId, int verticalAlignment) {
        this(context.getDrawable(resourceId), verticalAlignment);
    }
    @Override public Drawable getDrawable() { return drawable; }
}
