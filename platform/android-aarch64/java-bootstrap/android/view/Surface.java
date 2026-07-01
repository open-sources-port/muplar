package android.view;

public class Surface implements AutoCloseable {
    private SurfaceControl control;
    public Surface() {}
    public Surface(SurfaceControl control) { this.control = control; }
    public boolean isValid() { return control != null && control.isValid(); }
    public void release() { control = null; }
    @Override public void close() { release(); }
}
