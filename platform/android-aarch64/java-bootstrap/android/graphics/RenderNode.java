package android.graphics;

public class RenderNode {
    private final String name;
    public RenderNode(String name) { this.name = name == null ? "" : name; }
    public RecordingCanvas beginRecording(int width, int height) {
        return new RecordingCanvas();
    }
    public void endRecording() {}
    public String getName() { return name; }
}
