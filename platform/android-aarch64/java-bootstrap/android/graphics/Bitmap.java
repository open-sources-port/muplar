package android.graphics;

public class Bitmap {
    public enum Config { ALPHA_8, RGB_565, ARGB_4444, ARGB_8888, RGBA_F16, HARDWARE }
    private final int width;
    private final int height;
    private final Config config;
    Bitmap(int width, int height, Config config) {
        this.width = Math.max(1, width);
        this.height = Math.max(1, height);
        this.config = config;
    }
    public static Bitmap createBitmap(int width, int height, Config config) {
        return new Bitmap(width, height, config);
    }
    public static Bitmap createBitmap(Picture picture) {
        return new Bitmap(picture.getWidth(), picture.getHeight(), Config.HARDWARE);
    }
    public int getWidth() { return width; }
    public int getHeight() { return height; }
    public Config getConfig() { return config; }
    public void eraseColor(int color) {}
    public void recycle() {}
    public boolean isRecycled() { return false; }
}
