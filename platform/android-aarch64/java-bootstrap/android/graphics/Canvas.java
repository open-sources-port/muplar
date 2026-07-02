package android.graphics;

public class Canvas {
    private Bitmap bitmap;
    public Canvas() { bitmap = null; }
    public Canvas(Bitmap bitmap) { this.bitmap = bitmap; }
    public int getWidth() { return bitmap == null ? 0 : bitmap.getWidth(); }
    public int getHeight() { return bitmap == null ? 0 : bitmap.getHeight(); }
    public void setBitmap(Bitmap bitmap) { this.bitmap = bitmap; }
    public void setDrawFilter(DrawFilter filter) {}
    public int save() { return 1; }
    public void restore() {}
    public void restoreToCount(int saveCount) {}
    public void translate(float dx, float dy) {}
    public void scale(float sx, float sy) {}
    public void scale(float sx, float sy, float px, float py) {}
    public void rotate(float degrees) {}
    public void rotate(float degrees, float px, float py) {}
    public void drawColor(int color) {}
    public void drawPaint(Paint paint) {}
    public void drawCircle(float cx, float cy, float radius, Paint paint) {}
    public void drawRect(RectF rectangle, Paint paint) {}
    public void drawRoundRect(RectF rectangle, float rx, float ry, Paint paint) {}
    public void drawPath(Path path, Paint paint) {}
    public void drawBitmap(Bitmap bitmap, float left, float top, Paint paint) {}
}
