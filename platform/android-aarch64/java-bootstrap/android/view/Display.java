package android.view;

import android.util.DisplayMetrics;

public class Display {
    public static final int DEFAULT_DISPLAY = 0;
    public int getDisplayId() { return DEFAULT_DISPLAY; }
    public int getRotation() { return 0; }
    public void getMetrics(DisplayMetrics output) {
        DisplayMetrics current = new DisplayMetrics();
        output.widthPixels = current.widthPixels;
        output.heightPixels = current.heightPixels;
        output.densityDpi = current.densityDpi;
        output.density = current.density;
        output.scaledDensity = current.scaledDensity;
    }
}
