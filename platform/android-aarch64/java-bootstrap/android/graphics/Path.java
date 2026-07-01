package android.graphics;

public class Path {
    public enum Direction { CW, CCW }
    public enum FillType { WINDING, EVEN_ODD, INVERSE_WINDING, INVERSE_EVEN_ODD }
    public enum Op { DIFFERENCE, INTERSECT, UNION, XOR, REVERSE_DIFFERENCE }
    public Path() {}
    public Path(Path source) {}
    public void reset() {}
    public void rewind() {}
    public void moveTo(float x, float y) {}
    public void rMoveTo(float dx, float dy) {}
    public void lineTo(float x, float y) {}
    public void rLineTo(float dx, float dy) {}
    public void quadTo(float x1, float y1, float x2, float y2) {}
    public void cubicTo(float x1, float y1, float x2, float y2,
            float x3, float y3) {}
    public void close() {}
    public void addRect(float left, float top, float right, float bottom,
            Direction direction) {}
    public void addRoundRect(RectF rectangle, float rx, float ry,
            Direction direction) {}
    public void setFillType(FillType fillType) {}
    public boolean isEmpty() { return false; }
    public boolean op(Path path, Op operation) { return true; }
    public boolean op(Path first, Path second, Op operation) { return true; }
    public void transform(Matrix matrix) {}
    public void transform(Matrix matrix, Path destination) {}
}
