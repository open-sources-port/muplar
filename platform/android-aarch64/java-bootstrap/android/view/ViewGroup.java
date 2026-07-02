package android.view;

import com.muplar.runtime.HostUi;
import java.util.ArrayList;
import java.util.List;
import android.content.Context;
import android.util.AttributeSet;
import android.animation.LayoutTransition;

public class ViewGroup extends View implements ViewParent {
    public static final int FOCUS_BEFORE_DESCENDANTS = 0x20000;
    public static final int FOCUS_AFTER_DESCENDANTS = 0x40000;
    public static final int FOCUS_BLOCK_DESCENDANTS = 0x60000;
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
    private boolean motionEventSplittingEnabled = true;
    private boolean childrenDrawingOrderEnabled;
    private boolean clipToPadding = true;
    private boolean clipChildren = true;
    private LayoutTransition layoutTransition;
    private boolean alwaysDrawnWithCacheEnabled;
    private boolean childrenDrawnWithCacheEnabled;
    private int descendantFocusability = FOCUS_BEFORE_DESCENDANTS;
    private boolean touchIntercepted;
    private View touchTarget;

    protected ViewGroup(Object peer) { super(peer); }
    protected ViewGroup(Object peer, Context context) { super(peer, context); }
    public ViewGroup(Context context) { super(HostUi.createLinearLayout(), context); }
    public ViewGroup(Context context, AttributeSet attributes) { this(context); }
    public ViewGroup(Context context, AttributeSet attributes, int defStyleAttr) {
        this(context);
    }
    public ViewGroup(Context context, AttributeSet attributes, int defStyleAttr,
            int defStyleRes) { this(context); }

    public void setMotionEventSplittingEnabled(boolean enabled) {
        motionEventSplittingEnabled = enabled;
    }
    public boolean isMotionEventSplittingEnabled() {
        return motionEventSplittingEnabled;
    }
    protected void setChildrenDrawingOrderEnabled(boolean enabled) {
        childrenDrawingOrderEnabled = enabled;
    }
    protected int getChildDrawingOrder(int childCount, int drawingPosition) {
        return drawingPosition;
    }
    public void setClipToPadding(boolean enabled) { clipToPadding = enabled; }
    public boolean getClipToPadding() { return clipToPadding; }
    public void setClipChildren(boolean enabled) { clipChildren = enabled; }
    public boolean getClipChildren() { return clipChildren; }
    public boolean onInterceptTouchEvent(MotionEvent event) { return false; }
    @Override public boolean dispatchTouchEvent(MotionEvent event) {
        int action = event == null ? MotionEvent.ACTION_CANCEL : event.getActionMasked();
        if (action == MotionEvent.ACTION_DOWN) {
            touchIntercepted = false;
            touchTarget = null;
        }
        if (!touchIntercepted && onInterceptTouchEvent(event)) touchIntercepted = true;
        if (touchIntercepted) {
            boolean handled = onTouchEvent(event);
            if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
                touchIntercepted = false;
                touchTarget = null;
            }
            return handled;
        }

        if (event != null) {
            float x = event.getX();
            float y = event.getY();
            if (action != MotionEvent.ACTION_DOWN && touchTarget != null) {
                int left = touchTarget.getLeft();
                int top = touchTarget.getTop();
                event.offsetLocation(-left, -top);
                boolean handled = touchTarget.dispatchTouchEvent(event);
                event.offsetLocation(left, top);
                if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
                    touchTarget = null;
                }
                return handled;
            }
            if (action == MotionEvent.ACTION_DOWN) {
                for (int index = children.size() - 1; index >= 0; index--) {
                    View child = children.get(index);
                    if (child.getVisibility() == View.VISIBLE) {
                        int left = child.getLeft();
                        int top = child.getTop();
                        int right = child.getRight();
                        int bottom = child.getBottom();
                        if (x >= left && x < right && y >= top && y < bottom) {
                            event.offsetLocation(-left, -top);
                            boolean handled = child.dispatchTouchEvent(event);
                            event.offsetLocation(left, top);
                            if (handled) {
                                touchTarget = child;
                                return true;
                            }
                        }
                    }
                }
            }
        }

        boolean handled = super.dispatchTouchEvent(event);
        if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
            touchTarget = null;
        }
        return handled;
    }
    public void setLayoutTransition(LayoutTransition transition) {
        layoutTransition = transition;
    }
    public LayoutTransition getLayoutTransition() { return layoutTransition; }
    public void setAlwaysDrawnWithCacheEnabled(boolean enabled) {
        alwaysDrawnWithCacheEnabled = enabled;
    }
    public void setChildrenDrawnWithCacheEnabled(boolean enabled) {
        childrenDrawnWithCacheEnabled = enabled;
    }
    public void setDescendantFocusability(int focusability) {
        descendantFocusability = focusability;
    }
    public int getDescendantFocusability() { return descendantFocusability; }

    public void addView(View child) {
        if (child == null) return;
        if (child.getLayoutParams() == null)
            child.setLayoutParams(generateDefaultLayoutParams());
        children.add(child);
        child.assignParent(this);
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
        if (child.getLayoutParams() == null)
            child.setLayoutParams(generateDefaultLayoutParams());
        if (index < 0 || index > children.size()) index = children.size();
        children.add(index, child);
        child.assignParent(this);
        HostUi.addChild(getPeer(), child.getPeer());
        if (hierarchyChangeListener != null)
            hierarchyChangeListener.onChildViewAdded(this, child);
    }

    public void addView(View child, int index, LayoutParams params) {
        if (child != null) child.setLayoutParams(params);
        addView(child, index);
    }

    public int getChildCount() { return children.size(); }
    public View getChildAt(int index) {
        return index < 0 || index >= children.size() ? null : children.get(index);
    }
    public int indexOfChild(View child) { return children.indexOf(child); }
    public void removeView(View child) {
        if (!children.remove(child)) return;
        child.assignParent(null);
        if (hierarchyChangeListener != null)
            hierarchyChangeListener.onChildViewRemoved(this, child);
    }
    public void removeViewAt(int index) { removeView(children.get(index)); }
    public void removeAllViews() {
        List<View> removed = new ArrayList<View>(children);
        children.clear();
        for (View child : removed) child.assignParent(null);
        HostUi.removeAllChildren(getPeer());
        if (hierarchyChangeListener != null)
            for (View child : removed)
                hierarchyChangeListener.onChildViewRemoved(this, child);
    }
    @Override public void draw(android.graphics.Canvas canvas) {
        super.draw(canvas);
        dispatchDraw(canvas);
    }
    protected void dispatchDraw(android.graphics.Canvas canvas) {
        for (View child : new ArrayList<View>(children)) child.draw(canvas);
    }
    public void setOnHierarchyChangeListener(OnHierarchyChangeListener listener) {
        hierarchyChangeListener = listener;
    }

    protected LayoutParams generateDefaultLayoutParams() {
        return new LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT);
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
