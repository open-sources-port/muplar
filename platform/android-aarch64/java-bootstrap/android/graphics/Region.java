package android.graphics;

public class Region {
    private final Rect bounds = new Rect();
    public Region() {}
    public Region(Region source) { if (source != null) bounds.set(source.bounds); }
    public Region(Rect rectangle) { if (rectangle != null) bounds.set(rectangle); }
    public boolean isEmpty() { return bounds.isEmpty(); }
    public boolean set(Region source) {
        if (source == null) bounds.setEmpty(); else bounds.set(source.bounds);
        return !isEmpty();
    }
    public boolean set(Rect rectangle) {
        if (rectangle == null) bounds.setEmpty(); else bounds.set(rectangle);
        return !isEmpty();
    }
    public Rect getBounds() { return new Rect(bounds); }
    public void setEmpty() { bounds.setEmpty(); }
}
