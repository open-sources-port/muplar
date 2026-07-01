package android.view.animation;
public class DecelerateInterpolator implements Interpolator {
    private final float factor;
    public DecelerateInterpolator() { this(1.0f); }
    public DecelerateInterpolator(float factor) { this.factor = factor; }
    public float getInterpolation(float input) {
        return factor == 1.0f ? 1.0f - (1.0f - input) * (1.0f - input)
            : (float)(1.0 - Math.pow(1.0f - input, 2.0f * factor));
    }
}
