package android.animation;

import android.util.Property;

public class ObjectAnimator extends ValueAnimator {
    private Object target;
    private Property property;
    private Object finalValue;
    public static ObjectAnimator ofFloat(Object target, Property property,
            float... values) {
        ObjectAnimator animator = new ObjectAnimator();
        animator.target = target; animator.property = property;
        if (values != null && values.length > 0)
            animator.finalValue = Float.valueOf(values[values.length - 1]);
        return animator;
    }
    public static ObjectAnimator ofInt(Object target, Property property,
            int... values) {
        ObjectAnimator animator = new ObjectAnimator();
        animator.target = target; animator.property = property;
        if (values != null && values.length > 0)
            animator.finalValue = Integer.valueOf(values[values.length - 1]);
        return animator;
    }
    @Override public ObjectAnimator setDuration(long duration) {
        super.setDuration(duration); return this;
    }
    @Override protected void applyFinalValue() {
        if (target != null && property != null && finalValue != null)
            property.set(target, finalValue);
        super.applyFinalValue();
    }
}
