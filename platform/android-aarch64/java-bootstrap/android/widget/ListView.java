package android.widget;

import android.content.Context;
import android.view.View;
import android.view.ViewGroup;
import com.muplar.runtime.HostUi;

public class ListView extends ViewGroup {
    private BaseAdapter adapter;
    public ListView(Context context) {
        super(HostUi.createLinearLayout(), context);
        HostUi.setLinearLayoutOrientation(getPeer(), LinearLayout.VERTICAL);
    }
    public void setAdapter(BaseAdapter adapter) {
        this.adapter = adapter;
        HostUi.removeAllChildren(getPeer());
        if (adapter == null) return;
        for (int i = 0; i < adapter.getCount(); ++i)
            addView(adapter.getView(i, null, this));
    }
    public BaseAdapter getAdapter() { return adapter; }
}
