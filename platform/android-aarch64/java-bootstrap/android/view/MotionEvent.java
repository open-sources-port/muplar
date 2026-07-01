package android.view;

public class MotionEvent {
    public static final int ACTION_DOWN = 0;
    public static final int ACTION_UP = 1;
    public static final int ACTION_MOVE = 2;
    public static final int ACTION_CANCEL = 3;
    public static final int ACTION_MASK = 0xff;
    private int action;
    private float x;
    private float y;
    public int getAction() { return action; }
    public int getActionMasked() { return action & ACTION_MASK; }
    public float getX() { return x; }
    public float getY() { return y; }
    public float getRawX() { return x; }
    public float getRawY() { return y; }
    public static MotionEvent obtain(MotionEvent source) {
        MotionEvent event = new MotionEvent();
        if (source != null) {
            event.action = source.action; event.x = source.x; event.y = source.y;
        }
        return event;
    }
    public void recycle() {}
}
