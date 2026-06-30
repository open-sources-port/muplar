package android.view;

import com.muplar.runtime.HostUi;
import java.util.ArrayList;
import java.util.List;

public class ViewGroup extends View {
    private final List<View> children = new ArrayList<View>();

    protected ViewGroup(Object peer) { super(peer); }

    public void addView(View child) {
        if (child == null) return;
        children.add(child);
        HostUi.addChild(getPeer(), child.getPeer());
    }

    public int getChildCount() { return children.size(); }
    public View getChildAt(int index) { return children.get(index); }
    public void removeAllViews() {
        children.clear();
        HostUi.removeAllChildren(getPeer());
    }

    @Override public View findViewById(int wantedId) {
        View own = super.findViewById(wantedId);
        if (own != null) return own;
        for (View child : children) {
            View found = child.findViewById(wantedId);
            if (found != null) return found;
        }
        return null;
    }
}
