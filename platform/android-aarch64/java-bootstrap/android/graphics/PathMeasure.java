package android.graphics;

public class PathMeasure {
    private Path path;
    public PathMeasure() {}
    public PathMeasure(Path path, boolean forceClosed) { this.path = path; }
    public void setPath(Path path, boolean forceClosed) { this.path = path; }
    public float getLength() { return path == null ? 0.0f : 100.0f; }
    public boolean getPosTan(float distance, float[] position, float[] tangent) {
        if (path == null) return false;
        float fraction = Math.max(0.0f, Math.min(1.0f, distance / getLength()));
        if (position != null && position.length >= 2) {
            position[0] = fraction * 100.0f;
            position[1] = fraction * 100.0f;
        }
        if (tangent != null && tangent.length >= 2) {
            tangent[0] = 1.0f;
            tangent[1] = 1.0f;
        }
        return true;
    }
    public boolean getSegment(float start, float stop, Path destination,
            boolean startWithMoveTo) { return path != null; }
    public boolean nextContour() { return false; }
}
