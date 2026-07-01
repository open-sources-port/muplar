package android.view.animation;

import android.graphics.Path;

public class PathInterpolator implements Interpolator {
    public PathInterpolator(Path path) {}
    public PathInterpolator(float controlX, float controlY) {}
    public PathInterpolator(float x1, float y1, float x2, float y2) {}
    public float getInterpolation(float input) {
        return Math.max(0.0f, Math.min(1.0f, input));
    }
}
