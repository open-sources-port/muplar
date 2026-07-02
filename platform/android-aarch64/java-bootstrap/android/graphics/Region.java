package android.graphics;

public class Region {
    public enum Op { DIFFERENCE, INTERSECT, UNION, XOR, REVERSE_DIFFERENCE, REPLACE }
    private final Rect bounds = new Rect();
    public Region() {}
    public Region(int left, int top, int right, int bottom) {
        bounds.set(left, top, right, bottom);
    }
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
    public boolean setPath(Path path, Region clip) {
        return set(clip);
    }
    public boolean op(Region region, Op operation) {
        if (operation == Op.REPLACE) return set(region);
        return !isEmpty();
    }
    public void setEmpty() { bounds.setEmpty(); }
}
