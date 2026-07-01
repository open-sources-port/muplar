package android.view.animation;
public class BounceInterpolator implements Interpolator {
    private static float bounce(float value) { return value * value * 8.0f; }
    public float getInterpolation(float input) {
        float value = input * 1.1226f;
        if (value < 0.3535f) return bounce(value);
        if (value < 0.7408f) return bounce(value - 0.54719f) + 0.7f;
        if (value < 0.9644f) return bounce(value - 0.8526f) + 0.9f;
        return bounce(value - 1.0435f) + 0.95f;
    }
}
