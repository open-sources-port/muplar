package android.widget;

import android.content.Context;
import android.view.View;
import com.muplar.runtime.HostUi;

public class LinearLayout extends View {
    public static final int HORIZONTAL = 0;
    public static final int VERTICAL = 1;

    public LinearLayout(Context context) { super(HostUi.createLinearLayout()); }
    public void setOrientation(int orientation) {
        HostUi.setLinearLayoutOrientation(getPeer(), orientation);
    }
    public void addView(View child) {
        if (child != null) HostUi.addChild(getPeer(), child.getPeer());
    }
}
