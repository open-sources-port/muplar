package android.view;

public class MotionEvent {
    public static final int ACTION_DOWN = 0;
    public static final int ACTION_UP = 1;
    public static final int ACTION_MOVE = 2;
    public static final int ACTION_CANCEL = 3;
    public static final int ACTION_MASK = 0xff;
    public static final int CLASSIFICATION_NONE = 0;
    public static final int CLASSIFICATION_AMBIGUOUS_GESTURE = 1;
    public static final int CLASSIFICATION_DEEP_PRESS = 2;
    public static final int TOOL_TYPE_FINGER = 1;
    private int action;
    private float x;
    private float y;
    private long downTime;
    private long eventTime;
    public int getAction() { return action; }
    public int getActionMasked() { return action & ACTION_MASK; }
    public float getX() { return x; }
    public float getY() { return y; }
    public float getRawX() { return x; }
    public float getRawY() { return y; }
    public long getDownTime() { return downTime; }
    public long getEventTime() { return eventTime; }
    public int getPointerCount() { return 1; }
    public int getPointerId(int index) { return 0; }
    public int findPointerIndex(int pointerId) { return pointerId == 0 ? 0 : -1; }
    public int getActionIndex() { return 0; }
    public int getClassification() { return CLASSIFICATION_NONE; }
    public int getEdgeFlags() { return 0; }
    public int getFlags() { return 0; }
    public int getMetaState() { return 0; }
    public int getButtonState() { return 0; }
    public int getDeviceId() { return 0; }
    public int getSource() { return 0x1002; }
    public boolean isFromSource(int source) { return (getSource() & source) == source; }
    public int getToolType(int pointerIndex) { return TOOL_TYPE_FINGER; }
    public float getXPrecision() { return 1.0f; }
    public float getYPrecision() { return 1.0f; }
    public float getX(int pointerIndex) { return x; }
    public float getY(int pointerIndex) { return y; }
    public void setAction(int action) { this.action = action; }
    public void offsetLocation(float deltaX, float deltaY) {
        x += deltaX; y += deltaY;
    }
    public static MotionEvent obtain(long downTime, long eventTime, int action,
            float x, float y, int metaState) {
        MotionEvent event = new MotionEvent();
        event.downTime = downTime;
        event.eventTime = eventTime;
        event.action = action;
        event.x = x;
        event.y = y;
        return event;
    }
    public static MotionEvent obtain(MotionEvent source) {
        MotionEvent event = new MotionEvent();
        if (source != null) {
            event.action = source.action; event.x = source.x; event.y = source.y;
            event.downTime = source.downTime; event.eventTime = source.eventTime;
        }
        return event;
    }
    public void recycle() {}
}
