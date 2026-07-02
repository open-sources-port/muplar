package android.graphics;

public class Paint {
    public static final int ANTI_ALIAS_FLAG = 1;
    public static final int FILTER_BITMAP_FLAG = 2;
    public static final int DITHER_FLAG = 4;
    public enum Style { FILL, STROKE, FILL_AND_STROKE }
    public enum Align { LEFT, CENTER, RIGHT }
    private float textSize = 16.0f;
    private int color = 0xff000000;
    private float strokeWidth;
    private Xfermode xfermode;
    private Shader shader;
    private Typeface typeface;
    private MaskFilter maskFilter;
    public Paint() {}
    public Paint(int flags) {}
    public Paint(Paint source) {
        textSize = source.textSize; color = source.color; strokeWidth = source.strokeWidth;
    }
    public void setTextSize(float size) { textSize = size; }
    public float getTextSize() { return textSize; }
    public void setColor(int value) { color = value; }
    public int getColor() { return color; }
    public void setAlpha(int alpha) { color = (color & 0x00ffffff) | (alpha << 24); }
    public int getAlpha() { return color >>> 24; }
    public void setStrokeWidth(float width) { strokeWidth = width; }
    public float getStrokeWidth() { return strokeWidth; }
    public void setStyle(Style style) {}
    public void setTextAlign(Align align) {}
    public void setAntiAlias(boolean enabled) {}
    public void setFilterBitmap(boolean enabled) {}
    public void setShadowLayer(float radius, float dx, float dy, int color) {}
    public void clearShadowLayer() {}
    public Typeface setTypeface(Typeface value) {
        Typeface previous = typeface;
        typeface = value;
        return previous;
    }
    public Typeface getTypeface() { return typeface; }
    public Xfermode setXfermode(Xfermode value) {
        Xfermode previous = xfermode;
        xfermode = value;
        return previous;
    }
    public Xfermode getXfermode() { return xfermode; }
    public Shader setShader(Shader value) {
        Shader previous = shader; shader = value; return previous;
    }
    public Shader getShader() { return shader; }
    public MaskFilter setMaskFilter(MaskFilter value) {
        MaskFilter previous = maskFilter; maskFilter = value; return previous;
    }
    public MaskFilter getMaskFilter() { return maskFilter; }
    public float measureText(String text) {
        return text == null ? 0 : text.length() * textSize * 0.55f;
    }
    public FontMetrics getFontMetrics() {
        FontMetrics metrics = new FontMetrics();
        metrics.ascent = -textSize * 0.8f;
        metrics.descent = textSize * 0.2f;
        metrics.top = -textSize;
        metrics.bottom = textSize * 0.25f;
        metrics.leading = 0;
        return metrics;
    }
    public FontMetricsInt getFontMetricsInt() {
        FontMetricsInt metrics = new FontMetricsInt();
        getFontMetricsInt(metrics);
        return metrics;
    }
    public int getFontMetricsInt(FontMetricsInt metrics) {
        FontMetrics value = getFontMetrics();
        if (metrics != null) {
            metrics.ascent = Math.round(value.ascent);
            metrics.descent = Math.round(value.descent);
            metrics.top = Math.round(value.top);
            metrics.bottom = Math.round(value.bottom);
            metrics.leading = 0;
        }
        return Math.round(value.descent - value.ascent);
    }
    public static class FontMetrics {
        public float top, ascent, descent, bottom, leading;
    }
    public static class FontMetricsInt {
        public int top, ascent, descent, bottom, leading;
    }
}
