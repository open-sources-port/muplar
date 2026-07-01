package android.text;

public class StaticLayout extends Layout {
    private final Alignment alignment;

    private StaticLayout(CharSequence text, TextPaint paint, int width,
            Alignment alignment) {
        super(text, paint, width);
        this.alignment = alignment;
    }

    public static final class Builder {
        private final CharSequence text;
        private final TextPaint paint;
        private final int width;
        private Alignment alignment = Alignment.ALIGN_NORMAL;

        private Builder(CharSequence source, int start, int end,
                TextPaint paint, int width) {
            int safeStart = Math.max(0, Math.min(start, source.length()));
            int safeEnd = Math.max(safeStart, Math.min(end, source.length()));
            text = source.subSequence(safeStart, safeEnd);
            this.paint = paint;
            this.width = width;
        }
        public static Builder obtain(CharSequence source, int start, int end,
                TextPaint paint, int width) {
            return new Builder(source == null ? "" : source, start, end,
                paint, width);
        }
        public Builder setAlignment(Alignment value) {
            alignment = value;
            return this;
        }
        public StaticLayout build() {
            return new StaticLayout(text, paint, width, alignment);
        }
    }
}
