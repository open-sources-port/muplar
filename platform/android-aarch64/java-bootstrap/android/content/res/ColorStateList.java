package android.content.res;

public class ColorStateList {
    private final int defaultColor;
    public ColorStateList(int[][] states, int[] colors) {
        defaultColor = colors == null || colors.length == 0 ? 0 : colors[colors.length - 1];
    }
    private ColorStateList(int color) { defaultColor = color; }
    public static ColorStateList valueOf(int color) { return new ColorStateList(color); }
    public int getDefaultColor() { return defaultColor; }
    public int getColorForState(int[] stateSet, int fallback) { return defaultColor; }
    public boolean isStateful() { return false; }
}
