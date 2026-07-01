package android.widget;

import android.content.Context;
import android.util.AttributeSet;
import android.text.Editable;

public class EditText extends TextView {
    public EditText(Context context) { super(context); }
    public EditText(Context context, AttributeSet attributes) {
        super(context, attributes);
    }
    public EditText(Context context, AttributeSet attributes, int defStyleAttr) {
        super(context, attributes, defStyleAttr);
    }
    public EditText(Context context, AttributeSet attributes, int defStyleAttr,
            int defStyleRes) {
        super(context, attributes, defStyleAttr, defStyleRes);
    }
    @Override public Editable getText() { return getEditableText(); }
}
