package android.animation;

import java.util.ArrayList;

public class ValueAnimator extends Animator {
    public interface AnimatorUpdateListener {
        void onAnimationUpdate(ValueAnimator animation);
    }

    public static final int RESTART = 1;
    public static final int REVERSE = 2;
    public static final int INFINITE = -1;
    private final ArrayList<AnimatorUpdateListener> updateListeners = new ArrayList<>();
    private Object animatedValue;
    private float fraction;

    public static ValueAnimator ofFloat(float... values) {
        ValueAnimator animator = new ValueAnimator();
        if (values.length > 0) animator.animatedValue = values[values.length - 1];
        return animator;
    }
    public static ValueAnimator ofInt(int... values) {
        ValueAnimator animator = new ValueAnimator();
        if (values.length > 0) animator.animatedValue = values[values.length - 1];
        return animator;
    }
    public static ValueAnimator ofObject(TypeEvaluator evaluator, Object... values) {
        ValueAnimator animator = new ValueAnimator();
        if (values.length > 0) animator.animatedValue = values[values.length - 1];
        return animator;
    }
    public void addUpdateListener(AnimatorUpdateListener listener) {
        if (listener != null && !updateListeners.contains(listener))
            updateListeners.add(listener);
    }
    public void removeUpdateListener(AnimatorUpdateListener listener) {
        updateListeners.remove(listener);
    }
    public void removeAllUpdateListeners() { updateListeners.clear(); }
    public Object getAnimatedValue() { return animatedValue; }
    public float getAnimatedFraction() { return fraction; }
    public void setCurrentFraction(float value) { fraction = value; notifyUpdates(); }
    public void setCurrentPlayTime(long playTime) {}
    public long getCurrentPlayTime() { return 0; }
    public void setRepeatCount(int value) {}
    public void setRepeatMode(int value) {}

    @Override protected void applyFinalValue() {
        fraction = 1.0f;
        notifyUpdates();
    }
    private void notifyUpdates() {
        for (AnimatorUpdateListener listener : new ArrayList<>(updateListeners))
            listener.onAnimationUpdate(this);
    }
}
