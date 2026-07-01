package android.animation;

import android.graphics.Rect;

public class RectEvaluator implements TypeEvaluator<Rect> {
    private final Rect reuse;
    public RectEvaluator() { reuse = null; }
    public RectEvaluator(Rect reuse) { this.reuse = reuse; }
    public Rect evaluate(float fraction, Rect start, Rect end) {
        Rect result = reuse == null ? new Rect() : reuse;
        result.set(
            Math.round(start.left + (end.left - start.left) * fraction),
            Math.round(start.top + (end.top - start.top) * fraction),
            Math.round(start.right + (end.right - start.right) * fraction),
            Math.round(start.bottom + (end.bottom - start.bottom) * fraction));
        return result;
    }
}
