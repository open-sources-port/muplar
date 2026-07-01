package android.util;

public class TypedValue {
    public static final int COMPLEX_UNIT_PX = 0;
    public static final int COMPLEX_UNIT_DIP = 1;
    public static final int COMPLEX_UNIT_SP = 2;
    public static final int COMPLEX_UNIT_PT = 3;
    public static final int COMPLEX_UNIT_IN = 4;
    public static final int COMPLEX_UNIT_MM = 5;
    public int type;
    public int data;
    public int resourceId;
    public CharSequence string;
    public static float applyDimension(int unit, float value, DisplayMetrics metrics) {
        switch (unit) {
            case COMPLEX_UNIT_PX: return value;
            case COMPLEX_UNIT_DIP: return value * metrics.density;
            case COMPLEX_UNIT_SP: return value * metrics.scaledDensity;
            case COMPLEX_UNIT_PT: return value * metrics.densityDpi / 72.0f;
            case COMPLEX_UNIT_IN: return value * metrics.densityDpi;
            case COMPLEX_UNIT_MM: return value * metrics.densityDpi / 25.4f;
            default: return 0;
        }
    }
    public static float complexToFloat(int complex) {
        float[] factors = {1.0f / 256.0f, 1.0f / 32768.0f,
            1.0f / 8388608.0f, 1.0f / 2147483648.0f};
        return (complex & 0xffffff00) * factors[(complex >> 4) & 3];
    }
    public static float complexToDimension(int data, DisplayMetrics metrics) {
        return applyDimension(data & 15, complexToFloat(data), metrics);
    }
    public static int complexToDimensionPixelSize(int data, DisplayMetrics metrics) {
        float value = complexToDimension(data, metrics);
        int rounded = Math.round(value);
        return rounded != 0 ? rounded : value == 0 ? 0 : value > 0 ? 1 : -1;
    }
}
