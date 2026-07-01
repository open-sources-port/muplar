package android.view;

import com.muplar.runtime.HostUi;
import java.util.ArrayList;
import java.util.List;
import android.content.Context;

public class ViewGroup extends View {
    public static class LayoutParams {
        public static final int MATCH_PARENT = -1;
        public static final int WRAP_CONTENT = -2;
        public int width;
        public int height;
        public LayoutParams(int width, int height) {
            this.width = width;
            this.height = height;
        }
        public LayoutParams(LayoutParams source) {
            this(source.width, source.height);
        }
    }
    public static class MarginLayoutParams extends LayoutParams {
        public int leftMargin;
        public int topMargin;
        public int rightMargin;
        public int bottomMargin;
        public MarginLayoutParams(int width, int height) { super(width, height); }
        public MarginLayoutParams(LayoutParams source) { super(source); }
        public void setMargins(int left, int top, int right, int bottom) {
            leftMargin = left;
            topMargin = top;
            rightMargin = right;
            bottomMargin = bottom;
        }
    }
    public interface OnHierarchyChangeListener {
        void onChildViewAdded(View parent, View child);
        void onChildViewRemoved(View parent, View child);
    }

    private final List<View> children = new ArrayList<View>();
    private OnHierarchyChangeListener hierarchyChangeListener;

    protected ViewGroup(Object peer) { super(peer); }
    protected ViewGroup(Object peer, Context context) { super(peer, context); }

    public void addView(View child) {
        if (child == null) return;
        children.add(child);
        HostUi.addChild(getPeer(), child.getPeer());
        if (hierarchyChangeListener != null)
            hierarchyChangeListener.onChildViewAdded(this, child);
    }

    public void addView(View child, LayoutParams params) {
        if (child != null) child.setLayoutParams(params);
        addView(child);
    }

    public void addView(View child, int index) {
        if (child == null) return;
        if (index < 0 || index > children.size()) index = children.size();
        children.add(index, child);
        HostUi.addChild(getPeer(), child.getPeer());
        if (hierarchyChangeListener != null)
            hierarchyChangeListener.onChildViewAdded(this, child);
    }

    public int getChildCount() { return children.size(); }
    public View getChildAt(int index) { return children.get(index); }
    public int indexOfChild(View child) { return children.indexOf(child); }
    public void removeView(View child) {
        if (!children.remove(child)) return;
        if (hierarchyChangeListener != null)
            hierarchyChangeListener.onChildViewRemoved(this, child);
    }
    public void removeViewAt(int index) { removeView(children.get(index)); }
    public void removeAllViews() {
        List<View> removed = new ArrayList<View>(children);
        children.clear();
        HostUi.removeAllChildren(getPeer());
        if (hierarchyChangeListener != null)
            for (View child : removed)
                hierarchyChangeListener.onChildViewRemoved(this, child);
    }
    public void setOnHierarchyChangeListener(OnHierarchyChangeListener listener) {
        hierarchyChangeListener = listener;
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
