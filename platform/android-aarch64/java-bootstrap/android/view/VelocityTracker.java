package android.view;

public final class VelocityTracker {
    private float previousX;
    private float previousY;
    private float lastX;
    private float lastY;
    private float xVelocity;
    private float yVelocity;

    private VelocityTracker() {}
    public static VelocityTracker obtain() { return new VelocityTracker(); }
    public void addMovement(MotionEvent event) {
        if (event == null) return;
        previousX = lastX;
        previousY = lastY;
        lastX = event.getX();
        lastY = event.getY();
    }
    public void computeCurrentVelocity(int units) {
        computeCurrentVelocity(units, Float.MAX_VALUE);
    }
    public void computeCurrentVelocity(int units, float maxVelocity) {
        xVelocity = clamp((lastX - previousX) * units, maxVelocity);
        yVelocity = clamp((lastY - previousY) * units, maxVelocity);
    }
    public float getXVelocity() { return xVelocity; }
    public float getYVelocity() { return yVelocity; }
    public float getXVelocity(int pointerId) { return xVelocity; }
    public float getYVelocity(int pointerId) { return yVelocity; }
    public void clear() {
        previousX = previousY = lastX = lastY = xVelocity = yVelocity = 0;
    }
    public void recycle() { clear(); }
    private static float clamp(float value, float maximum) {
        return Math.max(-maximum, Math.min(maximum, value));
    }
}
