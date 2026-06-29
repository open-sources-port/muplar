package android.widget;

import android.content.Context;
import android.view.View;
import com.muplar.runtime.HostUi;

public class TextView extends View {
    public TextView(Context context) { super(HostUi.createTextView()); }
    protected TextView(Context context, Object peer) { super(peer); }
    public void setText(CharSequence text) {
        HostUi.setText(getPeer(), text == null ? "" : text.toString());
    }
}
