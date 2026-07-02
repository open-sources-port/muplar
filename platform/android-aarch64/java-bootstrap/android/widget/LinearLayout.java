package android.widget;

import android.content.Context;
import android.view.ViewGroup;
import com.muplar.runtime.HostUi;
import android.util.AttributeSet;

public class LinearLayout extends ViewGroup {
    public static class LayoutParams extends ViewGroup.MarginLayoutParams {
        public float weight;
        public int gravity = -1;
        public LayoutParams(int width, int height) { super(width, height); }
        public LayoutParams(int width, int height, float weight) {
            super(width, height); this.weight = weight;
        }
        public LayoutParams(ViewGroup.LayoutParams source) { super(source); }
    }
    public static final int HORIZONTAL = 0;
    public static final int VERTICAL = 1;

    public LinearLayout(Context context) { super(HostUi.createLinearLayout(), context); }
    public LinearLayout(Context context, AttributeSet attributes) { this(context); }
    public LinearLayout(Context context, AttributeSet attributes, int defStyleAttr) {
        this(context);
    }
    public LinearLayout(Context context, AttributeSet attributes, int defStyleAttr,
            int defStyleRes) { this(context); }
    public void setOrientation(int orientation) {
        HostUi.setLinearLayoutOrientation(getPeer(), orientation);
    }
    @Override protected ViewGroup.LayoutParams generateDefaultLayoutParams() {
        return new LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT);
    }
}
