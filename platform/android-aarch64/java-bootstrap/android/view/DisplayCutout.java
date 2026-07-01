package android.view;

import android.graphics.Insets;
import android.graphics.Rect;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

public final class DisplayCutout {
    private final Insets safeInsets;
    private final List<Rect> boundingRects;
    public DisplayCutout(Rect safeInsets, List<Rect> boundingRects) {
        this(Insets.of(safeInsets), boundingRects);
    }
    public DisplayCutout(Insets safeInsets, Rect left, Rect top,
            Rect right, Rect bottom) {
        this(safeInsets, collect(left, top, right, bottom));
    }
    private DisplayCutout(Insets safeInsets, List<Rect> bounds) {
        this.safeInsets = safeInsets == null ? Insets.NONE : safeInsets;
        this.boundingRects = bounds == null ? Collections.emptyList()
            : Collections.unmodifiableList(new ArrayList<>(bounds));
    }
    public int getSafeInsetLeft() { return safeInsets.left; }
    public int getSafeInsetTop() { return safeInsets.top; }
    public int getSafeInsetRight() { return safeInsets.right; }
    public int getSafeInsetBottom() { return safeInsets.bottom; }
    public Insets getWaterfallInsets() { return Insets.NONE; }
    public List<Rect> getBoundingRects() { return boundingRects; }
    public Rect getBoundingRectLeft() { return new Rect(); }
    public Rect getBoundingRectTop() { return new Rect(); }
    public Rect getBoundingRectRight() { return new Rect(); }
    public Rect getBoundingRectBottom() { return new Rect(); }
    public DisplayCutout getRotated(int startWidth, int startHeight,
            int fromRotation, int toRotation) { return this; }
    private static List<Rect> collect(Rect... rectangles) {
        List<Rect> result = new ArrayList<>();
        for (Rect rectangle : rectangles) if (rectangle != null) result.add(rectangle);
        return result;
    }
}
