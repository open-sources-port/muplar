package android.view;

import android.util.DisplayMetrics;
import android.graphics.Point;

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
    public void getSize(Point output) {
        DisplayMetrics metrics = new DisplayMetrics();
        getMetrics(metrics);
        output.set(metrics.widthPixels, metrics.heightPixels);
    }
    public void getRealSize(Point output) { getSize(output); }
}
