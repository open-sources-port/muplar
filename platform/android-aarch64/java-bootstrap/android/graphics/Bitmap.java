package android.graphics;

public class Bitmap {
    public enum Config { ALPHA_8, RGB_565, ARGB_4444, ARGB_8888, RGBA_F16, HARDWARE }
    public enum CompressFormat { JPEG, PNG, WEBP, WEBP_LOSSY, WEBP_LOSSLESS }
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
    public int getPixel(int x, int y) { return 0; }
    public void setPixel(int x, int y, int color) {}
    public void getPixels(int[] pixels, int offset, int stride, int x, int y,
            int width, int height) {
        if (pixels == null) return;
        int count = Math.min(pixels.length - Math.max(0, offset), width * height);
        for (int index = 0; index < count; index++) pixels[offset + index] = 0;
    }
    public void copyPixelsToBuffer(java.nio.Buffer destination) {
        int pixels = width * height;
        if (destination instanceof java.nio.ByteBuffer) {
            java.nio.ByteBuffer bytes = (java.nio.ByteBuffer) destination;
            int count = Math.min(bytes.remaining(), pixels * 4);
            for (int index = 0; index < count; index++) bytes.put((byte) 0);
        } else if (destination instanceof java.nio.IntBuffer) {
            java.nio.IntBuffer ints = (java.nio.IntBuffer) destination;
            int count = Math.min(ints.remaining(), pixels);
            for (int index = 0; index < count; index++) ints.put(0);
        } else if (destination instanceof java.nio.ShortBuffer) {
            java.nio.ShortBuffer shorts = (java.nio.ShortBuffer) destination;
            int count = Math.min(shorts.remaining(), pixels * 2);
            for (int index = 0; index < count; index++) shorts.put((short) 0);
        }
    }
    public void eraseColor(int color) {}
    public Bitmap extractAlpha(Paint paint, int[] offsetXY) {
        if (offsetXY != null && offsetXY.length >= 2) {
            offsetXY[0] = 0;
            offsetXY[1] = 0;
        }
        return new Bitmap(width, height, Config.ALPHA_8);
    }
    public boolean compress(CompressFormat format, int quality,
            java.io.OutputStream stream) {
        try {
            stream.write(new byte[] { 'M', 'U', 'P', 'L', 'A', 'R' });
            return true;
        } catch (java.io.IOException error) {
            return false;
        }
    }
    public void recycle() {}
    public boolean isRecycled() { return false; }
}
