package android.app;

public final class WallpaperColors {
    public static final int HINT_SUPPORTS_DARK_TEXT = 1;
    public static final int HINT_SUPPORTS_DARK_THEME = 2;
    private final int hints;
    public WallpaperColors() { this(0); }
    public WallpaperColors(int hints) { this.hints = hints; }
    public int getColorHints() { return hints; }
}
