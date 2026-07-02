package android.widget;

import android.content.Context;
import android.view.View;
import com.muplar.runtime.HostUi;
import android.util.AttributeSet;
import android.text.Editable;
import android.text.SpannableStringBuilder;
import android.text.TextWatcher;
import java.util.concurrent.CopyOnWriteArrayList;
import android.view.KeyEvent;
import android.content.res.ColorStateList;
import android.graphics.drawable.Drawable;
import android.util.TypedValue;
import android.text.TextUtils;

public class TextView extends View {
    public interface OnEditorActionListener {
        boolean onEditorAction(TextView view, int actionId, KeyEvent event);
    }
    private final CopyOnWriteArrayList<TextWatcher> textWatchers =
        new CopyOnWriteArrayList<TextWatcher>();
    private Editable text = new SpannableStringBuilder();
    private OnEditorActionListener editorActionListener;
    private CharSequence hint = "";
    private ColorStateList textColors = ColorStateList.valueOf(0xff000000);
    private final Drawable[] compoundDrawables = new Drawable[4];
    private int compoundDrawablePadding;
    private float textSize = 16.0f;
    private TextUtils.TruncateAt ellipsize;
    private int maxLines = Integer.MAX_VALUE;
    public TextView(Context context) { super(HostUi.createTextView(), context); }
    public TextView(Context context, AttributeSet attributes) { this(context); }
    public TextView(Context context, AttributeSet attributes, int defStyleAttr) {
        this(context);
    }
    public TextView(Context context, AttributeSet attributes, int defStyleAttr,
            int defStyleRes) { this(context); }
    protected TextView(Context context, Object peer) { super(peer, context); }
    public void setText(CharSequence text) {
        CharSequence value = text == null ? "" : text;
        CharSequence old = this.text.toString();
        for (TextWatcher watcher : textWatchers)
            watcher.beforeTextChanged(old, 0, old.length(), value.length());
        this.text = new SpannableStringBuilder(value);
        HostUi.setText(getPeer(), value.toString());
        for (TextWatcher watcher : textWatchers) {
            watcher.onTextChanged(value, 0, old.length(), value.length());
            watcher.afterTextChanged(this.text);
        }
    }
    public void setText(int resourceId) { setText(getResources().getText(resourceId)); }
    public CharSequence getText() { return text; }
    protected Editable getEditableText() { return text; }
    public void setHint(CharSequence value) { hint = value == null ? "" : value; }
    public void setHint(int resourceId) { setHint(getResources().getText(resourceId)); }
    public CharSequence getHint() { return hint; }
    public void setTextColor(int color) { textColors = ColorStateList.valueOf(color); }
    public void setTextColor(ColorStateList colors) {
        textColors = colors == null ? ColorStateList.valueOf(0) : colors;
    }
    public ColorStateList getTextColors() { return textColors; }
    public int getCurrentTextColor() { return textColors.getDefaultColor(); }
    public void setCompoundDrawablesRelative(Drawable start, Drawable top,
            Drawable end, Drawable bottom) {
        compoundDrawables[0] = start;
        compoundDrawables[1] = top;
        compoundDrawables[2] = end;
        compoundDrawables[3] = bottom;
    }
    public void setCompoundDrawables(Drawable left, Drawable top,
            Drawable right, Drawable bottom) {
        compoundDrawables[0] = left;
        compoundDrawables[1] = top;
        compoundDrawables[2] = right;
        compoundDrawables[3] = bottom;
    }
    public Drawable[] getCompoundDrawables() { return compoundDrawables.clone(); }
    public Drawable[] getCompoundDrawablesRelative() {
        return compoundDrawables.clone();
    }
    public void setCompoundDrawablePadding(int padding) {
        compoundDrawablePadding = padding;
    }
    public void setCompoundDrawablesRelativeWithIntrinsicBounds(int start,
            int top, int end, int bottom) {
        setCompoundDrawablesRelative(
            start == 0 ? null : getResources().getDrawable(start),
            top == 0 ? null : getResources().getDrawable(top),
            end == 0 ? null : getResources().getDrawable(end),
            bottom == 0 ? null : getResources().getDrawable(bottom));
    }
    public int getCompoundDrawablePadding() { return compoundDrawablePadding; }
    public void setTextSize(int unit, float size) {
        textSize = TypedValue.applyDimension(unit, size,
            getResources().getDisplayMetrics());
    }
    public void setTextSize(float size) { setTextSize(TypedValue.COMPLEX_UNIT_SP, size); }
    public float getTextSize() { return textSize; }
    public void setEllipsize(TextUtils.TruncateAt where) { ellipsize = where; }
    public TextUtils.TruncateAt getEllipsize() { return ellipsize; }
    public void setMaxLines(int value) { maxLines = value; }
    public int getMaxLines() { return maxLines; }
    public void setShadowLayer(float radius, float dx, float dy, int color) {}
    public void addTextChangedListener(TextWatcher watcher) {
        if (watcher != null) textWatchers.add(watcher);
    }
    public void removeTextChangedListener(TextWatcher watcher) {
        textWatchers.remove(watcher);
    }
    public void setOnEditorActionListener(OnEditorActionListener listener) {
        editorActionListener = listener;
    }
    public boolean onEditorAction(int actionId) {
        return editorActionListener != null &&
            editorActionListener.onEditorAction(this, actionId, null);
    }
}
