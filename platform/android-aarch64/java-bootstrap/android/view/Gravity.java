package android.view;

import android.graphics.Rect;

public final class Gravity {
    public static final int NO_GRAVITY = 0;
    public static final int TOP = 0x30;
    public static final int BOTTOM = 0x50;
    public static final int LEFT = 0x03;
    public static final int RIGHT = 0x05;
    public static final int CENTER_VERTICAL = 0x10;
    public static final int FILL_VERTICAL = 0x70;
    public static final int CENTER_HORIZONTAL = 0x01;
    public static final int FILL_HORIZONTAL = 0x07;
    public static final int CENTER = CENTER_VERTICAL | CENTER_HORIZONTAL;
    public static final int START = 0x00800003;
    public static final int END = 0x00800005;
    private Gravity() {}
    public static boolean isVertical(int gravity) {
        return (gravity & FILL_VERTICAL) != 0;
    }
    public static boolean isHorizontal(int gravity) {
        return (gravity & FILL_HORIZONTAL) != 0;
    }
    public static int getAbsoluteGravity(int gravity, int layoutDirection) {
        int result = gravity;
        if ((result & START) == START)
            result = (result & ~START) |
                (layoutDirection == View.LAYOUT_DIRECTION_RTL ? RIGHT : LEFT);
        if ((result & END) == END)
            result = (result & ~END) |
                (layoutDirection == View.LAYOUT_DIRECTION_RTL ? LEFT : RIGHT);
        return result;
    }
    public static void apply(int gravity, int width, int height,
            Rect container, Rect output) {
        int left = (gravity & RIGHT) == RIGHT ? container.right - width
            : (gravity & CENTER_HORIZONTAL) == CENTER_HORIZONTAL
                ? container.left + (container.width() - width) / 2
                : container.left;
        int top = (gravity & BOTTOM) == BOTTOM ? container.bottom - height
            : (gravity & CENTER_VERTICAL) == CENTER_VERTICAL
                ? container.top + (container.height() - height) / 2
                : container.top;
        output.set(left, top, left + width, top + height);
    }
    public static void apply(int gravity, int width, int height,
            Rect container, int xAdjustment, int yAdjustment, Rect output) {
        apply(gravity, width, height, container, output);
        output.offset(xAdjustment, yAdjustment);
    }
}
