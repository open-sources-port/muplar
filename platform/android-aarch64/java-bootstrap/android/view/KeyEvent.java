package android.view;

public class KeyEvent {
    public static final int ACTION_DOWN = 0;
    public static final int ACTION_UP = 1;
    public static final int ACTION_MULTIPLE = 2;
    public static final int KEYCODE_UNKNOWN = 0;
    public static final int KEYCODE_BACK = 4;
    public static final int KEYCODE_HOME = 3;
    public static final int KEYCODE_ENTER = 66;
    public static final int KEYCODE_MENU = 82;
    public static final int KEYCODE_DEL = 67;
    public static final int META_SHIFT_ON = 1;
    public static final int META_ALT_ON = 2;
    public static final int META_CTRL_ON = 4096;

    private final int action;
    private final int keyCode;
    private final int metaState;
    public KeyEvent(int action, int keyCode) { this(0, 0, action, keyCode, 0); }
    public KeyEvent(long downTime, long eventTime, int action, int keyCode,
            int repeat) { this(downTime, eventTime, action, keyCode, repeat, 0); }
    public KeyEvent(long downTime, long eventTime, int action, int keyCode,
            int repeat, int metaState) {
        this.action = action;
        this.keyCode = keyCode;
        this.metaState = metaState;
    }
    public int getAction() { return action; }
    public int getKeyCode() { return keyCode; }
    public int getMetaState() { return metaState; }
    public int getRepeatCount() { return 0; }
    public boolean isShiftPressed() { return (metaState & META_SHIFT_ON) != 0; }
    public boolean isAltPressed() { return (metaState & META_ALT_ON) != 0; }
    public boolean isCtrlPressed() { return (metaState & META_CTRL_ON) != 0; }
}
