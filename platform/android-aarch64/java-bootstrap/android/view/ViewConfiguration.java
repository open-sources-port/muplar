package android.view;

import android.content.Context;

public final class ViewConfiguration {
    private static final ViewConfiguration INSTANCE = new ViewConfiguration();
    public static ViewConfiguration get(Context context) { return INSTANCE; }
    public int getScaledTouchSlop() { return 8; }
    public int getScaledPagingTouchSlop() { return 16; }
    public int getScaledMaximumFlingVelocity() { return 8000; }
    public int getScaledMinimumFlingVelocity() { return 50; }
    public float getScaledHorizontalScrollFactor() { return 64.0f; }
    public float getScaledVerticalScrollFactor() { return 64.0f; }
    public static int getLongPressTimeout() { return 500; }
    public static int getTapTimeout() { return 100; }
    public static int getScrollBarFadeDuration() { return 250; }
    public static int getScrollDefaultDelay() { return 300; }
}
