package android.view.animation;
public class OvershootInterpolator implements Interpolator {
    private final float tension;
    public OvershootInterpolator() { this(2.0f); }
    public OvershootInterpolator(float tension) { this.tension = tension; }
    public float getInterpolation(float input) {
        float value = input - 1.0f;
        return value * value * ((tension + 1.0f) * value + tension) + 1.0f;
    }
}
