package android.widget;

import android.content.Context;
import android.graphics.Rect;
import android.text.Editable;
import android.text.SpannableStringBuilder;
import android.util.AttributeSet;
import android.view.KeyEvent;

public class EditText extends TextView {
    private SpannableStringBuilder editable = new SpannableStringBuilder();

    public EditText(Context context) {
        super(context);
    }

    public EditText(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public EditText(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
    }

    public EditText(Context context,
                    AttributeSet attrs,
                    int defStyleAttr,
                    int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
    }

    @Override
    public Editable getText() {
        return editable;
    }

    @Override
    public void setText(CharSequence text, BufferType type) {
        editable = new SpannableStringBuilder(text == null ? "" : text);
        super.setText(editable, BufferType.SPANNABLE);
    }

    public void setSelection(int index) {
        android.text.Selection.setSelection(editable, index);
    }

    public void extendSelection(int index) {
        android.text.Selection.extendSelection(editable, index);
    }

    public boolean isSuggestionsEnabled() {
        return false;
    }

    public boolean onKeyPreIme(int keyCode, KeyEvent event) {
        return false;
    }

    @Override
    protected void onFocusChanged(boolean focused,
                                  int direction,
                                  Rect previouslyFocusedRect) {
        super.onFocusChanged(focused, direction, previouslyFocusedRect);
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (event != null && event.getAction() == KeyEvent.ACTION_DOWN) {
            if (onKeyDown(event.getKeyCode(), event)) {
                return true;
            }
        }
        return super.dispatchKeyEvent(event);
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        if (!isEnabled()) {
            return super.onKeyDown(keyCode, event);
        }
        int selStart = android.text.Selection.getSelectionStart(editable);
        int selEnd = android.text.Selection.getSelectionEnd(editable);
        if (selStart < 0) selStart = editable.length();
        if (selEnd < 0) selEnd = editable.length();

        if (keyCode == KeyEvent.KEYCODE_DEL) {
            if (selStart != selEnd) {
                int start = Math.min(selStart, selEnd);
                int end = Math.max(selStart, selEnd);
                editable.delete(start, end);
                setSelection(start);
                super.setText(editable, BufferType.SPANNABLE);
                return true;
            } else if (selStart > 0) {
                editable.delete(selStart - 1, selStart);
                setSelection(selStart - 1);
                super.setText(editable, BufferType.SPANNABLE);
                return true;
            }
            return true;
        }

        if (keyCode == KeyEvent.KEYCODE_FORWARD_DEL) {
            if (selStart != selEnd) {
                int start = Math.min(selStart, selEnd);
                int end = Math.max(selStart, selEnd);
                editable.delete(start, end);
                setSelection(start);
                super.setText(editable, BufferType.SPANNABLE);
                return true;
            } else if (selEnd < editable.length()) {
                editable.delete(selEnd, selEnd + 1);
                setSelection(selEnd);
                super.setText(editable, BufferType.SPANNABLE);
                return true;
            }
            return true;
        }

        if (keyCode == KeyEvent.KEYCODE_DPAD_LEFT) {
            if (selStart > 0) {
                setSelection(selStart - 1);
            }
            return true;
        }

        if (keyCode == KeyEvent.KEYCODE_DPAD_RIGHT) {
            if (selEnd < editable.length()) {
                setSelection(selEnd + 1);
            }
            return true;
        }

        if (keyCode == KeyEvent.KEYCODE_MOVE_HOME) {
            setSelection(0);
            return true;
        }

        if (keyCode == KeyEvent.KEYCODE_MOVE_END) {
            setSelection(editable.length());
            return true;
        }

        if (keyCode == KeyEvent.KEYCODE_ENTER) {
            int start = Math.min(selStart, selEnd);
            int end = Math.max(selStart, selEnd);
            editable.replace(start, end, "\n");
            setSelection(start + 1);
            super.setText(editable, BufferType.SPANNABLE);
            return true;
        }

        if (keyCode == KeyEvent.KEYCODE_TAB) {
            return super.onKeyDown(keyCode, event);
        }

        int unicode = event != null ? event.getUnicodeChar() : 0;
        if (unicode == 0) {
            unicode = com.muplar.runtime.FrameworkDeviceController.sLastUnicodeChar;
        }
        if (unicode == 0 && event != null) {
            unicode = fallbackUnicodeChar(keyCode, event.getMetaState());
        }
        if (unicode > 0 && !Character.isISOControl(unicode)) {
            int start = Math.min(selStart, selEnd);
            int end = Math.max(selStart, selEnd);
            editable.replace(start, end, String.valueOf((char) unicode));
            setSelection(start + 1);
            super.setText(editable, BufferType.SPANNABLE);
            return true;
        }

        return super.onKeyDown(keyCode, event);
    }

    private static int fallbackUnicodeChar(int keyCode, int metaState) {
        boolean shift = (metaState & KeyEvent.META_SHIFT_ON) != 0;
        boolean caps = (metaState & 0x100000 /* META_CAPS_LOCK_ON */) != 0;
        boolean upper = shift ^ caps;

        if (keyCode >= KeyEvent.KEYCODE_A && keyCode <= KeyEvent.KEYCODE_Z) {
            char base = (char) ('a' + (keyCode - KeyEvent.KEYCODE_A));
            return upper ? Character.toUpperCase(base) : base;
        }
        if (keyCode >= KeyEvent.KEYCODE_0 && keyCode <= KeyEvent.KEYCODE_9) {
            if (shift) {
                switch (keyCode) {
                    case KeyEvent.KEYCODE_1: return '!';
                    case KeyEvent.KEYCODE_2: return '@';
                    case KeyEvent.KEYCODE_3: return '#';
                    case KeyEvent.KEYCODE_4: return '$';
                    case KeyEvent.KEYCODE_5: return '%';
                    case KeyEvent.KEYCODE_6: return '^';
                    case KeyEvent.KEYCODE_7: return '&';
                    case KeyEvent.KEYCODE_8: return '*';
                    case KeyEvent.KEYCODE_9: return '(';
                    case KeyEvent.KEYCODE_0: return ')';
                }
            } else {
                return '0' + (keyCode - KeyEvent.KEYCODE_0);
            }
        }
        if (keyCode == KeyEvent.KEYCODE_SPACE) return ' ';
        if (keyCode == KeyEvent.KEYCODE_ENTER) return '\n';
        if (keyCode == KeyEvent.KEYCODE_TAB) return '\t';
        if (shift) {
            switch (keyCode) {
                case KeyEvent.KEYCODE_GRAVE: return '~';
                case KeyEvent.KEYCODE_MINUS: return '_';
                case KeyEvent.KEYCODE_EQUALS: return '+';
                case KeyEvent.KEYCODE_LEFT_BRACKET: return '{';
                case KeyEvent.KEYCODE_RIGHT_BRACKET: return '}';
                case KeyEvent.KEYCODE_BACKSLASH: return '|';
                case KeyEvent.KEYCODE_SEMICOLON: return ':';
                case KeyEvent.KEYCODE_APOSTROPHE: return '"';
                case KeyEvent.KEYCODE_COMMA: return '<';
                case KeyEvent.KEYCODE_PERIOD: return '>';
                case KeyEvent.KEYCODE_SLASH: return '?';
            }
        } else {
            switch (keyCode) {
                case KeyEvent.KEYCODE_GRAVE: return '`';
                case KeyEvent.KEYCODE_MINUS: return '-';
                case KeyEvent.KEYCODE_EQUALS: return '=';
                case KeyEvent.KEYCODE_LEFT_BRACKET: return '[';
                case KeyEvent.KEYCODE_RIGHT_BRACKET: return ']';
                case KeyEvent.KEYCODE_BACKSLASH: return '\\';
                case KeyEvent.KEYCODE_SEMICOLON: return ';';
                case KeyEvent.KEYCODE_APOSTROPHE: return '\'';
                case KeyEvent.KEYCODE_COMMA: return ',';
                case KeyEvent.KEYCODE_PERIOD: return '.';
                case KeyEvent.KEYCODE_SLASH: return '/';
            }
        }
        return 0;
    }

    @Override
    public CharSequence getAccessibilityClassName() {
        return EditText.class.getName();
    }
}
