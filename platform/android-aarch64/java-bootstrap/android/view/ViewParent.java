package android.view;

public interface ViewParent {
    default ViewParent getParent() { return null; }
    default boolean onStartNestedScroll(View child, View target, int axes) {
        return false;
    }
    default void onNestedScrollAccepted(View child, View target, int axes) {}
    default void onStopNestedScroll(View target) {}
    default void onNestedScroll(View target, int dxConsumed, int dyConsumed,
            int dxUnconsumed, int dyUnconsumed) {}
    default void onNestedPreScroll(View target, int dx, int dy, int[] consumed) {}
    default boolean onNestedFling(View target, float velocityX, float velocityY,
            boolean consumed) { return false; }
    default boolean onNestedPreFling(View target, float velocityX,
            float velocityY) { return false; }
    default int getNestedScrollAxes() { return 0; }
    default void requestDisallowInterceptTouchEvent(boolean disallowIntercept) {}
}
