package android.view;

import android.content.Context;

public abstract class OrientationEventListener {
    public static final int ORIENTATION_UNKNOWN = -1;
    public static final int SENSOR_DELAY_NORMAL = 3;
    public OrientationEventListener(Context context) {}
    public OrientationEventListener(Context context, int rate) {}
    public abstract void onOrientationChanged(int orientation);
    public void enable() {}
    public void disable() {}
    public boolean canDetectOrientation() { return false; }
}
