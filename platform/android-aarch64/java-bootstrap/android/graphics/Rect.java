package android.graphics;

public class Rect {
    public int left, top, right, bottom;
    public Rect() {}
    public Rect(int left, int top, int right, int bottom) {
        this.left = left; this.top = top; this.right = right; this.bottom = bottom;
    }
    public Rect(Rect source) { this(source.left, source.top, source.right, source.bottom); }
    public int width() { return right - left; }
    public int height() { return bottom - top; }
    public boolean isEmpty() { return left >= right || top >= bottom; }
    public void set(int left, int top, int right, int bottom) {
        this.left = left; this.top = top; this.right = right; this.bottom = bottom;
    }
    public void set(Rect source) { set(source.left, source.top, source.right, source.bottom); }
    public void setEmpty() { set(0, 0, 0, 0); }
    public int centerX() { return (left + right) >> 1; }
    public int centerY() { return (top + bottom) >> 1; }
    public float exactCenterX() { return (left + right) * 0.5f; }
    public float exactCenterY() { return (top + bottom) * 0.5f; }
    public void offset(int dx, int dy) { left += dx; right += dx; top += dy; bottom += dy; }
    public void inset(int dx, int dy) { left += dx; right -= dx; top += dy; bottom -= dy; }
    public void inset(int left, int top, int right, int bottom) {
        this.left += left;
        this.top += top;
        this.right -= right;
        this.bottom -= bottom;
    }
    public boolean contains(int x, int y) {
        return x >= left && x < right && y >= top && y < bottom;
    }
}
