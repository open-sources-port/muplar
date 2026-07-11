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
    public CharSequence getAccessibilityClassName() {
        return EditText.class.getName();
    }
}
