package android.graphics;

public class PointF {
    public float x;
    public float y;
    public PointF() {}
    public PointF(float x, float y) { this.x = x; this.y = y; }
    public PointF(PointF source) { this(source.x, source.y); }
    public void set(float x, float y) { this.x = x; this.y = y; }
    public void set(PointF source) {
        if (source == null) set(0, 0); else set(source.x, source.y);
    }
    public void offset(float dx, float dy) { x += dx; y += dy; }
    public float length() { return length(x, y); }
    public static float length(float x, float y) {
        return (float)Math.hypot(x, y);
    }
    @Override public boolean equals(Object other) {
        return other instanceof PointF && ((PointF)other).x == x && ((PointF)other).y == y;
    }
    @Override public int hashCode() {
        return 31 * Float.floatToIntBits(x) + Float.floatToIntBits(y);
    }
}
