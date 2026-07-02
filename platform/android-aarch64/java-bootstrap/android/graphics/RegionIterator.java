package android.graphics;

public class RegionIterator {
    private final Rect bounds;
    private boolean consumed;
    public RegionIterator(Region region) {
        bounds = region == null ? new Rect() : region.getBounds();
    }
    public boolean next(Rect rectangle) {
        if (consumed || bounds.isEmpty() || rectangle == null) return false;
        rectangle.set(bounds);
        consumed = true;
        return true;
    }
}
