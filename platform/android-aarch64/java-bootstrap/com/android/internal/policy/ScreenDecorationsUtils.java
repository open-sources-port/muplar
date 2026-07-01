package com.android.internal.policy;

import android.content.res.Resources;
import android.content.Context;

public final class ScreenDecorationsUtils {
    private ScreenDecorationsUtils() {}

    public static boolean supportsRoundedCornersOnWindows(Resources resources) {
        return false;
    }
    public static float getWindowCornerRadius(Context context) { return 0.0f; }
}
