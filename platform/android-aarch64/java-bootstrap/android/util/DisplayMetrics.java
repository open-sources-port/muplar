package android.util;

import java.awt.Dimension;
import java.awt.Toolkit;

public class DisplayMetrics {
    public static final int DENSITY_DEFAULT = 160;
    public static final int DENSITY_DEVICE_STABLE = DENSITY_DEFAULT;
    public int densityDpi = 480;
    public float density = 3.0f;
    public float scaledDensity = 3.0f;
    public int widthPixels = 1242;
    public int heightPixels = 2688;

    public DisplayMetrics() {
        // Use default phone metrics to ensure phone layout rendering
    }
}
