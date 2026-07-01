package android.view.animation;
public class AnticipateInterpolator implements Interpolator {
    private final float tension;
    public AnticipateInterpolator() { this(2.0f); }
    public AnticipateInterpolator(float tension) { this.tension = tension; }
    public float getInterpolation(float input) {
        return input * input * ((tension + 1.0f) * input - tension);
    }
}
