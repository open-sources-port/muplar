package android.view.animation;
public class CycleInterpolator implements Interpolator {
    private final float cycles;
    public CycleInterpolator(float cycles) { this.cycles = cycles; }
    public float getInterpolation(float input) {
        return (float)Math.sin(2.0f * cycles * Math.PI * input);
    }
}
