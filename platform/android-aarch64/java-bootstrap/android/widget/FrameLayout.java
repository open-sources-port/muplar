package android.widget;

import android.content.Context;
import android.util.AttributeSet;
import android.view.ViewGroup;
import com.muplar.runtime.HostUi;

public class FrameLayout extends ViewGroup {
    public FrameLayout(Context context) { super(HostUi.createLinearLayout(), context); }
    public FrameLayout(Context context, AttributeSet attrs) { this(context); }
    public FrameLayout(Context context, AttributeSet attrs, int defStyleAttr) {
        this(context);
    }
    public FrameLayout(Context context, AttributeSet attrs, int defStyleAttr,
            int defStyleRes) { this(context); }

    @Override protected ViewGroup.LayoutParams generateDefaultLayoutParams() {
        return new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT);
    }

    public static class LayoutParams extends ViewGroup.MarginLayoutParams {
        public int gravity = -1;
        public LayoutParams(int width, int height) { super(width, height); }
        public LayoutParams(int width, int height, int gravity) {
            super(width, height);
            this.gravity = gravity;
        }
        public LayoutParams(ViewGroup.LayoutParams source) { super(source); }
    }
}
