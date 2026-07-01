package android.widget;

import android.content.Context;
import android.util.AttributeSet;
import android.view.ViewGroup;
import com.muplar.runtime.HostUi;

public class RelativeLayout extends ViewGroup {
    public RelativeLayout(Context context) {
        super(HostUi.createLinearLayout(), context);
    }
    public RelativeLayout(Context context, AttributeSet attributes) { this(context); }
    public RelativeLayout(Context context, AttributeSet attributes, int defStyleAttr) {
        this(context);
    }
    public RelativeLayout(Context context, AttributeSet attributes, int defStyleAttr,
            int defStyleRes) { this(context); }

    @Override protected ViewGroup.LayoutParams generateDefaultLayoutParams() {
        return new LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT);
    }

    public static class LayoutParams extends ViewGroup.MarginLayoutParams {
        public LayoutParams(int width, int height) { super(width, height); }
        public LayoutParams(ViewGroup.LayoutParams source) { super(source); }
        public void addRule(int verb) {}
        public void addRule(int verb, int anchor) {}
        public void removeRule(int verb) {}
    }
}
