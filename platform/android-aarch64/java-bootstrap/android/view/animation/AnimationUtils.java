package android.view.animation;

import android.content.Context;
import android.os.SystemClock;

public final class AnimationUtils {
    private AnimationUtils() {}
    public static Interpolator loadInterpolator(Context context, int id) {
        return new LinearInterpolator();
    }
    public static long currentAnimationTimeMillis() {
        return SystemClock.uptimeMillis();
    }
}
