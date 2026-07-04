package android.app.prediction;

public final class AppTargetId {
    private final String id;
    public AppTargetId(String id) { this.id = id == null ? "" : id; }
    public String getId() { return id; }
    @Override public String toString() { return id; }
}
