package android.graphics;

public final class Color {
    public static final int TRANSPARENT = 0x00000000;
    public static final int BLACK = 0xff000000;
    public static final int WHITE = 0xffffffff;
    public static final int RED = 0xffff0000;
    public static final int GREEN = 0xff00ff00;
    public static final int BLUE = 0xff0000ff;
    private Color() {}
    public static int alpha(int color) { return color >>> 24; }
    public static int red(int color) { return (color >> 16) & 255; }
    public static int green(int color) { return (color >> 8) & 255; }
    public static int blue(int color) { return color & 255; }
    public static int rgb(int red, int green, int blue) {
        return argb(255, red, green, blue);
    }
    public static int argb(int alpha, int red, int green, int blue) {
        return (alpha & 255) << 24 | (red & 255) << 16 |
            (green & 255) << 8 | (blue & 255);
    }
    public static int parseColor(String color) {
        if (color == null || color.charAt(0) != '#')
            throw new IllegalArgumentException("Unknown color");
        long value = Long.parseLong(color.substring(1), 16);
        if (color.length() == 7) value |= 0xff000000L;
        return (int)value;
    }
}
