package android.graphics;

public class Picture {
    private int width;
    private int height;
    public Picture() {}
    public Canvas beginRecording(int width, int height) {
        this.width = width;
        this.height = height;
        return new Canvas(Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888));
    }
    public void endRecording() {}
    public int getWidth() { return width; }
    public int getHeight() { return height; }
}
