package android.view;

import android.graphics.Insets;

public class WindowInsets {
    public static final WindowInsets CONSUMED = new WindowInsets();
    public static final class Type {
        public static int statusBars() { return 1; }
        public static int navigationBars() { return 2; }
        public static int systemBars() { return statusBars() | navigationBars(); }
        public static int displayCutout() { return 4; }
        public static int ime() { return 8; }
    }
    public boolean isVisible(int typeMask) { return false; }
    public DisplayCutout getDisplayCutout() { return null; }
    public Insets getInsets(int typeMask) { return Insets.NONE; }
    public Insets getInsetsIgnoringVisibility(int typeMask) { return Insets.NONE; }
    public WindowInsets inset(int left, int top, int right, int bottom) { return this; }
    public WindowInsets consumeDisplayCutout() { return this; }
    public WindowInsets consumeSystemWindowInsets() { return this; }

    public static final class Builder {
        public Builder() {}
        public Builder(WindowInsets source) {}
        public Builder setInsets(int typeMask, Insets insets) { return this; }
        public Builder setInsetsIgnoringVisibility(int typeMask, Insets insets) {
            return this;
        }
        public Builder setVisible(int typeMask, boolean visible) { return this; }
        public Builder setDisplayCutout(DisplayCutout cutout) { return this; }
        public WindowInsets build() { return new WindowInsets(); }
    }
}
