package android.widget;

import android.content.Context;
import android.view.View;
import android.view.ViewGroup;
import com.muplar.runtime.HostUi;
import android.util.AttributeSet;

public class ListView extends ViewGroup {
    private BaseAdapter adapter;
    public ListView(Context context) {
        super(HostUi.createLinearLayout(), context);
        HostUi.setLinearLayoutOrientation(getPeer(), LinearLayout.VERTICAL);
    }
    public ListView(Context context, AttributeSet attributes) { this(context); }
    public ListView(Context context, AttributeSet attributes, int defStyleAttr) {
        this(context);
    }
    public ListView(Context context, AttributeSet attributes, int defStyleAttr,
            int defStyleRes) { this(context); }
    public void setAdapter(BaseAdapter adapter) {
        this.adapter = adapter;
        HostUi.removeAllChildren(getPeer());
        if (adapter == null) return;
        for (int i = 0; i < adapter.getCount(); ++i)
            addView(adapter.getView(i, null, this));
    }
    public BaseAdapter getAdapter() { return adapter; }
}
