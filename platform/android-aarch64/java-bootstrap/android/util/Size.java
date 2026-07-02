package android.util;

public final class Size {
    private final int width;
    private final int height;
    public Size(int width, int height) { this.width = width; this.height = height; }
    public int getWidth() { return width; }
    public int getHeight() { return height; }
    @Override public boolean equals(Object other) {
        return other instanceof Size && ((Size) other).width == width &&
            ((Size) other).height == height;
    }
    @Override public int hashCode() { return 31 * width + height; }
    @Override public String toString() { return width + "x" + height; }
}
