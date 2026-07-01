package android.widget;

import android.content.Context;

public class Toast {
    public static final int LENGTH_SHORT = 0;
    public static final int LENGTH_LONG = 1;
    private CharSequence text;
    private Toast(CharSequence text) { this.text = text; }
    public static Toast makeText(Context context, CharSequence text, int duration) {
        return new Toast(text);
    }
    public static Toast makeText(Context context, int resourceId, int duration) {
        return new Toast(context == null ? "" : context.getString(resourceId));
    }
    public void setText(CharSequence text) { this.text = text; }
    public void show() {
        if (text != null && text.length() > 0)
            System.out.println("[Toast] " + text);
    }
    public void cancel() {}
}
