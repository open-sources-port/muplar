package android.widget;

import android.content.Context;
import android.view.View;
import com.muplar.runtime.HostUi;
import android.graphics.drawable.Drawable;

public class Button extends TextView {
    public Button(Context context) { super(context, HostUi.createButton()); }
    protected Button(Context context, Object peer) { super(context, peer); }
    public void setOnClickListener(final View.OnClickListener listener) {
        HostUi.setOnClickListener(getPeer(), listener == null ? null : new Runnable() {
            @Override public void run() { listener.onClick(Button.this); }
        });
    }
    public void setIconPath(String path) {
        HostUi.setButtonIcon(getPeer(), path);
    }
    public void setCompoundDrawablesWithIntrinsicBounds(
            Drawable left, Drawable top, Drawable right, Drawable bottom) {
        HostUi.setButtonIcon(getPeer(), left == null ? null :
            left.getSourcePath());
    }
}
