package android.graphics;
public class PorterDuffXfermode extends Xfermode {
    private final PorterDuff.Mode mode;
    public PorterDuffXfermode(PorterDuff.Mode mode) { this.mode = mode; }
    public PorterDuff.Mode getMode() { return mode; }
}
