package android.view;

import android.content.Context;

public class Window {
    private final View decorView;
    private int statusBarColor;
    private int navigationBarColor;
    private int softInputMode;
    private int privateFlags;
    public Window(Context context) { decorView = new View(context); }
    public View getDecorView() { return decorView; }
    public void addFlags(int flags) {}
    public void clearFlags(int flags) {}
    public void addPrivateFlags(int flags) { privateFlags |= flags; }
    public void setStatusBarColor(int color) { statusBarColor = color; }
    public int getStatusBarColor() { return statusBarColor; }
    public void setNavigationBarColor(int color) { navigationBarColor = color; }
    public int getNavigationBarColor() { return navigationBarColor; }
    public void setNavigationBarDividerColor(int color) {}
    public void setSoftInputMode(int mode) { softInputMode = mode; }
    public int getSoftInputMode() { return softInputMode; }
}
