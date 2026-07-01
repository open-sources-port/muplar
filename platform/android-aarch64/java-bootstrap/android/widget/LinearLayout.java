package android.widget;

import android.content.Context;
import android.view.ViewGroup;
import com.muplar.runtime.HostUi;
import android.util.AttributeSet;

public class LinearLayout extends ViewGroup {
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
}
