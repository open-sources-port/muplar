package android.view;

public class WindowInsetsController {
    public static final int BEHAVIOR_DEFAULT = 0;
    public static final int BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE = 2;
    private int behavior;
    public void hide(int types) {}
    public void show(int types) {}
    public void setSystemBarsBehavior(int value) { behavior = value; }
    public int getSystemBarsBehavior() { return behavior; }
    public void setSystemBarsAppearance(int appearance, int mask) {}
}
