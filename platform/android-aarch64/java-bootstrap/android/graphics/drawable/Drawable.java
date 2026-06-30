package android.graphics.drawable;

public class Drawable {
    private final String sourcePath;

    public Drawable(String sourcePath) {
        this.sourcePath = sourcePath;
    }

    public String getSourcePath() {
        return sourcePath;
    }
}
