package android.graphics;

public class Matrix {
    public static final int MSCALE_X = 0;
    public static final int MSKEW_X = 1;
    public static final int MTRANS_X = 2;
    public static final int MSKEW_Y = 3;
    public static final int MSCALE_Y = 4;
    public static final int MTRANS_Y = 5;
    public static final int MPERSP_0 = 6;
    public static final int MPERSP_1 = 7;
    public static final int MPERSP_2 = 8;
    private final float[] values = new float[9];

    public Matrix() { reset(); }
    public Matrix(Matrix source) { set(source); }
    public void reset() {
        for (int i = 0; i < 9; i++) values[i] = 0;
        values[MSCALE_X] = values[MSCALE_Y] = values[MPERSP_2] = 1;
    }
    public void set(Matrix source) {
        System.arraycopy(source.values, 0, values, 0, 9);
    }
    public boolean isIdentity() {
        return values[0] == 1 && values[4] == 1 && values[8] == 1 &&
            values[1] == 0 && values[2] == 0 && values[3] == 0 &&
            values[5] == 0 && values[6] == 0 && values[7] == 0;
    }
    public void setTranslate(float dx, float dy) {
        reset(); values[MTRANS_X] = dx; values[MTRANS_Y] = dy;
    }
    public void setScale(float sx, float sy) {
        reset(); values[MSCALE_X] = sx; values[MSCALE_Y] = sy;
    }
    public boolean postTranslate(float dx, float dy) {
        values[MTRANS_X] += dx; values[MTRANS_Y] += dy; return true;
    }
    public boolean preTranslate(float dx, float dy) { return postTranslate(dx, dy); }
    public boolean postScale(float sx, float sy) {
        values[MSCALE_X] *= sx; values[MSCALE_Y] *= sy;
        values[MTRANS_X] *= sx; values[MTRANS_Y] *= sy; return true;
    }
    public boolean postScale(float sx, float sy, float px, float py) {
        postTranslate(-px, -py); postScale(sx, sy); postTranslate(px, py); return true;
    }
    public boolean postRotate(float degrees) { return postRotate(degrees, 0, 0); }
    public boolean postRotate(float degrees, float px, float py) {
        double radians = Math.toRadians(degrees);
        float cos = (float)Math.cos(radians);
        float sin = (float)Math.sin(radians);
        float a = values[0], b = values[1], c = values[2];
        float d = values[3], e = values[4], f = values[5];
        values[0] = cos * a - sin * d;
        values[1] = cos * b - sin * e;
        values[2] = cos * (c - px) - sin * (f - py) + px;
        values[3] = sin * a + cos * d;
        values[4] = sin * b + cos * e;
        values[5] = sin * (c - px) + cos * (f - py) + py;
        return true;
    }
    public void mapPoints(float[] points) { mapPoints(points, points); }
    public void mapPoints(float[] destination, float[] source) {
        for (int i = 0; i + 1 < source.length; i += 2) {
            float x = source[i], y = source[i + 1];
            destination[i] = values[0] * x + values[1] * y + values[2];
            destination[i + 1] = values[3] * x + values[4] * y + values[5];
        }
    }
    public boolean mapRect(RectF rectangle) {
        float[] points = {rectangle.left, rectangle.top, rectangle.right, rectangle.bottom};
        mapPoints(points);
        rectangle.set(Math.min(points[0], points[2]), Math.min(points[1], points[3]),
            Math.max(points[0], points[2]), Math.max(points[1], points[3]));
        return true;
    }
    public void getValues(float[] destination) {
        System.arraycopy(values, 0, destination, 0, Math.min(9, destination.length));
    }
    public void setValues(float[] source) {
        if (source.length < 9) throw new ArrayIndexOutOfBoundsException();
        System.arraycopy(source, 0, values, 0, 9);
    }
    public boolean invert(Matrix inverse) {
        float determinant = values[0] * values[4] - values[1] * values[3];
        if (determinant == 0) return false;
        float inv = 1.0f / determinant;
        inverse.reset();
        inverse.values[0] = values[4] * inv;
        inverse.values[1] = -values[1] * inv;
        inverse.values[3] = -values[3] * inv;
        inverse.values[4] = values[0] * inv;
        inverse.values[2] = -(inverse.values[0] * values[2] + inverse.values[1] * values[5]);
        inverse.values[5] = -(inverse.values[3] * values[2] + inverse.values[4] * values[5]);
        return true;
    }
}
