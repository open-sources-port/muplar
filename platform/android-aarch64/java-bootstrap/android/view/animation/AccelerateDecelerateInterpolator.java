package android.view.animation;
public class AccelerateDecelerateInterpolator implements Interpolator {
    public float getInterpolation(float input) {
        return (float)(Math.cos((input + 1.0f) * Math.PI) / 2.0f) + 0.5f;
    }
}
