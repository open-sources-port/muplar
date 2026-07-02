package android.view.accessibility;

public class AccessibilityEvent {
    public static final int TYPE_VIEW_SCROLLED = 0x1000;
    private int eventType;
    private int scrollX;
    private int scrollY;
    private int fromIndex;
    private int toIndex;
    private int itemCount;
    public static AccessibilityEvent obtain() { return new AccessibilityEvent(); }
    public static AccessibilityEvent obtain(int eventType) {
        AccessibilityEvent event = new AccessibilityEvent();
        event.eventType = eventType;
        return event;
    }
    public int getEventType() { return eventType; }
    public void setScrollX(int value) { scrollX = value; }
    public void setScrollY(int value) { scrollY = value; }
    public void setFromIndex(int value) { fromIndex = value; }
    public void setToIndex(int value) { toIndex = value; }
    public void setItemCount(int value) { itemCount = value; }
    public int getScrollX() { return scrollX; }
    public int getScrollY() { return scrollY; }
    public int getFromIndex() { return fromIndex; }
    public int getToIndex() { return toIndex; }
    public int getItemCount() { return itemCount; }
    public void recycle() {}
}
