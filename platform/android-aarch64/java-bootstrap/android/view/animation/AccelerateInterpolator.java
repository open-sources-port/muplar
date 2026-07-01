package android.view.animation;
public class AccelerateInterpolator implements Interpolator {
    private final float factor;
    public AccelerateInterpolator() { this(1.0f); }
    public AccelerateInterpolator(float factor) { this.factor = factor; }
    public float getInterpolation(float input) {
        return factor == 1.0f ? input * input
            : (float)Math.pow(input, factor * 2.0f);
    }
}
