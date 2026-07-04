package android.view;

import android.graphics.Insets;
import java.util.List;

public class WindowInsetsAnimation {
    public static final class Bounds {
        private final Insets lower;
        private final Insets upper;
        public Bounds(Insets lower, Insets upper) {
            this.lower = lower; this.upper = upper;
        }
        public Insets getLowerBound() { return lower; }
        public Insets getUpperBound() { return upper; }
    }
    public abstract static class Callback {
        public static final int DISPATCH_MODE_STOP = 0;
        public static final int DISPATCH_MODE_CONTINUE_ON_SUBTREE = 1;
        private final int dispatchMode;
        public Callback(int dispatchMode) { this.dispatchMode = dispatchMode; }
        public int getDispatchMode() { return dispatchMode; }
        public void onPrepare(WindowInsetsAnimation animation) {}
        public Bounds onStart(WindowInsetsAnimation animation, Bounds bounds) { return bounds; }
        public WindowInsets onProgress(WindowInsets insets,
                List<WindowInsetsAnimation> runningAnimations) { return insets; }
        public void onEnd(WindowInsetsAnimation animation) {}
    }
}
