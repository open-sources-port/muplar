package android.graphics;

public class Typeface {
    public static final int NORMAL = 0;
    public static final int BOLD = 1;
    public static final int ITALIC = 2;
    public static final int BOLD_ITALIC = 3;
    public static final Typeface DEFAULT = new Typeface("default", NORMAL);
    public static final Typeface DEFAULT_BOLD = new Typeface("default", BOLD);
    public static final Typeface SANS_SERIF = new Typeface("sans", NORMAL);
    public static final Typeface SERIF = new Typeface("serif", NORMAL);
    public static final Typeface MONOSPACE = new Typeface("monospace", NORMAL);

    private final String family;
    private final int style;

    private Typeface(String family, int style) {
        this.family = family;
        this.style = style;
    }

    public static Typeface create(String familyName, int style) {
        return new Typeface(familyName == null ? "default" : familyName, style);
    }
    public static Typeface create(Typeface family, int style) {
        return new Typeface(family == null ? "default" : family.family, style);
    }
    public int getStyle() { return style; }
    public boolean isBold() { return (style & BOLD) != 0; }
    public boolean isItalic() { return (style & ITALIC) != 0; }
}
