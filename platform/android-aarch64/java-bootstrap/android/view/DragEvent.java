package android.view;

import android.content.ClipData;
import android.content.ClipDescription;

public final class DragEvent {
    public static final int ACTION_DRAG_STARTED = 1;
    public static final int ACTION_DRAG_LOCATION = 2;
    public static final int ACTION_DROP = 3;
    public static final int ACTION_DRAG_ENDED = 4;
    public static final int ACTION_DRAG_ENTERED = 5;
    public static final int ACTION_DRAG_EXITED = 6;
    private final int action;
    private final float x;
    private final float y;
    private final Object localState;
    private final ClipDescription description;
    private final ClipData data;
    private final boolean result;

    private DragEvent(int action, float x, float y, Object localState,
            ClipDescription description, ClipData data, boolean result) {
        this.action = action;
        this.x = x;
        this.y = y;
        this.localState = localState;
        this.description = description;
        this.data = data;
        this.result = result;
    }
    public static DragEvent obtain(int action, float x, float y, Object localState,
            ClipDescription description, ClipData data, boolean result) {
        return new DragEvent(action, x, y, localState, description, data, result);
    }
    public int getAction() { return action; }
    public float getX() { return x; }
    public float getY() { return y; }
    public Object getLocalState() { return localState; }
    public ClipDescription getClipDescription() { return description; }
    public ClipData getClipData() { return data; }
    public boolean getResult() { return result; }
    public void recycle() {}
}
