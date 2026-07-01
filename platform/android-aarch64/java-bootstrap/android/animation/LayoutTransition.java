package android.animation;

public class LayoutTransition {
    public static final int CHANGE_APPEARING = 0;
    public static final int APPEARING = 1;
    public static final int DISAPPEARING = 2;
    public static final int CHANGE_DISAPPEARING = 3;
    public void enableTransitionType(int transitionType) {}
    public void disableTransitionType(int transitionType) {}
    public void setInterpolator(int transitionType,
            TimeInterpolator interpolator) {}
    public boolean isRunning() { return false; }
}
