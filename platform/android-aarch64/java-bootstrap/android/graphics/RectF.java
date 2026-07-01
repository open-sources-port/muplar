package android.graphics;

public class RectF {
    public float left;
    public float top;
    public float right;
    public float bottom;
    public RectF() {}
    public RectF(float left, float top, float right, float bottom) {
        set(left, top, right, bottom);
    }
    public RectF(RectF source) { set(source); }
    public RectF(Rect source) {
        this(source.left, source.top, source.right, source.bottom);
    }
    public void set(float left, float top, float right, float bottom) {
        this.left = left; this.top = top; this.right = right; this.bottom = bottom;
    }
    public void set(RectF source) {
        set(source.left, source.top, source.right, source.bottom);
    }
    public float width() { return right - left; }
    public float height() { return bottom - top; }
    public float centerX() { return (left + right) * 0.5f; }
    public float centerY() { return (top + bottom) * 0.5f; }
    public boolean isEmpty() { return left >= right || top >= bottom; }
    public void offset(float dx, float dy) {
        left += dx; right += dx; top += dy; bottom += dy;
    }
    public void offsetTo(float newLeft, float newTop) {
        offset(newLeft - left, newTop - top);
    }
    public void inset(float dx, float dy) {
        left += dx; right -= dx; top += dy; bottom -= dy;
    }
    public void setEmpty() { set(0, 0, 0, 0); }
    public boolean contains(float x, float y) {
        return x >= left && x < right && y >= top && y < bottom;
    }
    public void roundOut(Rect destination) {
        destination.set((int)Math.floor(left), (int)Math.floor(top),
            (int)Math.ceil(right), (int)Math.ceil(bottom));
    }
}
