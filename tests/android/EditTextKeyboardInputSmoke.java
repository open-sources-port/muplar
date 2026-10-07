import android.view.KeyEvent;
import android.widget.EditText;
import com.muplar.runtime.MuplarContext;

public final class EditTextKeyboardInputSmoke {
    private static void assertEquals(Object expected, Object actual, String message) {
        if (expected == null && actual == null) return;
        if (expected != null && expected.equals(actual)) return;
        throw new AssertionError(message + ": expected <" + expected + "> but got <" + actual + ">");
    }

    private static void assertTrue(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    private static KeyEvent makeKeyEvent(int action, int keyCode, int metaState) {
        long now = System.currentTimeMillis();
        try {
            java.lang.reflect.Constructor<KeyEvent> ctor =
                KeyEvent.class.getConstructor(
                    long.class, long.class, int.class, int.class, int.class,
                    int.class, int.class, int.class, int.class, int.class);
            return ctor.newInstance(
                now, now, action, keyCode, 0 /* repeat */,
                metaState, 1 /* deviceId */,
                0 /* scancode */, 0 /* flags */,
                0x101 /* SOURCE_KEYBOARD */);
        } catch (Throwable t) {
            return new KeyEvent(action, keyCode);
        }
    }

    private static void sendKey(EditText editText, int keyCode, int metaState) {
        KeyEvent down = makeKeyEvent(KeyEvent.ACTION_DOWN, keyCode, metaState);
        KeyEvent up = makeKeyEvent(KeyEvent.ACTION_UP, keyCode, metaState);
        editText.dispatchKeyEvent(down);
        editText.dispatchKeyEvent(up);
    }

    public static void main(String[] args) throws Exception {
        MuplarContext context = new MuplarContext("com.muplar.keyboardtest", null, null);
        EditText editText = new EditText(context);
        editText.setText("", android.widget.TextView.BufferType.SPANNABLE);

        // 1. Basic typing: "abc"
        sendKey(editText, KeyEvent.KEYCODE_A, 0);
        sendKey(editText, KeyEvent.KEYCODE_B, 0);
        sendKey(editText, KeyEvent.KEYCODE_C, 0);
        assertEquals("abc", editText.getText().toString(), "Basic typing failed");

        // 2. Shift typing: Shift + D -> "D"
        sendKey(editText, KeyEvent.KEYCODE_D, KeyEvent.META_SHIFT_ON);
        assertEquals("abcD", editText.getText().toString(), "Shift typing failed");

        // 3. Space and numbers: Space, 1, 2
        sendKey(editText, KeyEvent.KEYCODE_SPACE, 0);
        sendKey(editText, KeyEvent.KEYCODE_1, 0);
        sendKey(editText, KeyEvent.KEYCODE_2, 0);
        assertEquals("abcD 12", editText.getText().toString(), "Space and numbers failed");

        // 4. Shift symbol: Shift + 1 -> '!'
        sendKey(editText, KeyEvent.KEYCODE_1, KeyEvent.META_SHIFT_ON);
        assertEquals("abcD 12!", editText.getText().toString(), "Shift symbol failed");

        // 5. Backspace: KEYCODE_DEL
        sendKey(editText, KeyEvent.KEYCODE_DEL, 0);
        assertEquals("abcD 12", editText.getText().toString(), "Backspace failed");

        // 6. DPAD Navigation and Insert: DPAD_LEFT twice, type 'X'
        sendKey(editText, KeyEvent.KEYCODE_DPAD_LEFT, 0);
        sendKey(editText, KeyEvent.KEYCODE_DPAD_LEFT, 0);
        sendKey(editText, KeyEvent.KEYCODE_X, 0);
        assertEquals("abcD X12", editText.getText().toString(), "DPAD Navigation and Insert failed");

        // 7. Selection range delete
        editText.setSelection(1);
        editText.extendSelection(4); // selects "bcD"
        sendKey(editText, KeyEvent.KEYCODE_DEL, 0);
        assertEquals("a X12", editText.getText().toString(), "Selection delete failed");

        // 8. Move to Home and End
        sendKey(editText, KeyEvent.KEYCODE_MOVE_HOME, 0);
        sendKey(editText, KeyEvent.KEYCODE_Z, 0);
        assertEquals("Za X12", editText.getText().toString(), "Move Home failed");

        sendKey(editText, KeyEvent.KEYCODE_MOVE_END, 0);
        sendKey(editText, KeyEvent.KEYCODE_PERIOD, 0);
        assertEquals("Za X12.", editText.getText().toString(), "Move End failed");

        System.out.println("[KeyboardTest] ALL EDITTEXT KEYBOARD INPUT TESTS PASSED!");
    }
}
