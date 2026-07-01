package android.util;

import java.awt.Dimension;
import java.awt.Toolkit;

public class DisplayMetrics {
    public static final int DENSITY_DEFAULT = 160;
    public static final int DENSITY_DEVICE_STABLE = DENSITY_DEFAULT;
    public int densityDpi = DENSITY_DEFAULT;
    public float density = 1.0f;
    public float scaledDensity = 1.0f;
    public int widthPixels = 420;
    public int heightPixels = 280;

    public DisplayMetrics() {
        try {
            Dimension screen = Toolkit.getDefaultToolkit().getScreenSize();
            widthPixels = screen.width;
            heightPixels = screen.height;
            densityDpi = Toolkit.getDefaultToolkit().getScreenResolution();
            density = densityDpi / 160.0f;
            scaledDensity = density;
        } catch (RuntimeException ignored) {}
    }
}
