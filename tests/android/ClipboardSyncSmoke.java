import android.content.ClipData;
import android.content.ClipDescription;
import android.content.ClipboardManager;
import android.content.Context;
import android.view.KeyEvent;
import android.widget.EditText;
import com.muplar.runtime.MuplarContext;
import com.muplar.runtime.MuplarServices;
import java.util.concurrent.atomic.AtomicBoolean;

public final class ClipboardSyncSmoke {
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
        MuplarServices.install();
        MuplarContext context = new MuplarContext("com.muplar.clipboardtest", null, null);
        ClipboardManager cm = (ClipboardManager) context.getSystemService(Context.CLIPBOARD_SERVICE);
        assertTrue(cm != null, "ClipboardManager must not be null");

        // 1. Initial clear state
        cm.clearPrimaryClip();
        assertTrue(!cm.hasPrimaryClip(), "Initial clipboard should not have primary clip");
        assertEquals(null, cm.getPrimaryClip(), "Initial primary clip should be null");

        // 2. Add listener and set text
        final AtomicBoolean listenerCalled = new AtomicBoolean(false);
        ClipboardManager.OnPrimaryClipChangedListener listener =
            new ClipboardManager.OnPrimaryClipChangedListener() {
                @Override
                public void onPrimaryClipChanged() {
                    listenerCalled.set(true);
                }
            };
        cm.addPrimaryClipChangedListener(listener);

        ClipData clip = ClipData.newPlainText("sample", "Hello Muplar Clipboard");
        cm.setPrimaryClip(clip);

        assertTrue(cm.hasPrimaryClip(), "Clipboard should report hasPrimaryClip() == true");
        assertTrue(listenerCalled.get(), "Listener should have been notified of clipboard change");

        ClipData retrieved = cm.getPrimaryClip();
        assertTrue(retrieved != null, "Retrieved clip should not be null");
        assertTrue(retrieved.getItemCount() > 0, "Retrieved clip must have at least one item");
        assertEquals("Hello Muplar Clipboard", retrieved.getItemAt(0).getText().toString(),
                     "Clipboard text should match");

        ClipDescription desc = cm.getPrimaryClipDescription();
        assertTrue(desc != null, "Primary clip description should not be null");
        assertTrue(desc.hasMimeType(ClipDescription.MIMETYPE_TEXT_PLAIN),
                   "Primary clip should have text/plain MIME type");

        // 3. Test EditText integration (Cmd+C and Cmd+V)
        EditText editText = new EditText(context);
        editText.setText("Sample Text", android.widget.TextView.BufferType.SPANNABLE);

        // Select "Text" (indices 7 to 11)
        editText.setSelection(7);
        editText.extendSelection(11);

        // Send Cmd+C (0x10000 is META_COMMAND_ON)
        sendKey(editText, KeyEvent.KEYCODE_C, 0x10000);

        ClipData copyClip = cm.getPrimaryClip();
        assertTrue(copyClip != null && copyClip.getItemCount() > 0, "Copy clip should exist");
        assertEquals("Text", copyClip.getItemAt(0).getText().toString(),
                     "Cmd+C should have copied selected text 'Text'");

        // Clear EditText and send Cmd+V
        editText.setText("", android.widget.TextView.BufferType.SPANNABLE);
        sendKey(editText, KeyEvent.KEYCODE_V, 0x10000);
        assertEquals("Text", editText.getText().toString(),
                     "Cmd+V should have pasted 'Text'");

        // Select all with Cmd+A and replace with Del
        sendKey(editText, KeyEvent.KEYCODE_A, 0x10000);
        sendKey(editText, KeyEvent.KEYCODE_DEL, 0);
        assertEquals("", editText.getText().toString(),
                     "Cmd+A followed by Del should clear EditText");

        // 4. Test removePrimaryClipChangedListener and clearPrimaryClip
        listenerCalled.set(false);
        cm.removePrimaryClipChangedListener(listener);
        cm.clearPrimaryClip();
        assertTrue(!listenerCalled.get(), "Removed listener should not be called on clear");
        assertTrue(!cm.hasPrimaryClip(), "Clipboard should be empty after clearPrimaryClip");

        System.out.println("[ClipboardTest] ALL CLIPBOARD SYNCHRONIZATION TESTS PASSED!");
    }
}

