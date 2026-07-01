package android.widget;

import android.content.Context;
import com.muplar.runtime.HostUi;
import android.util.AttributeSet;

public class CheckBox extends Button {
    public CheckBox(Context context) { super(context, HostUi.createCheckBox()); }
    public CheckBox(Context context, AttributeSet attributes) { this(context); }
    public CheckBox(Context context, AttributeSet attributes, int defStyleAttr) {
        this(context);
    }
    public CheckBox(Context context, AttributeSet attributes, int defStyleAttr,
            int defStyleRes) { this(context); }
    public boolean isChecked() { return HostUi.isChecked(getPeer()); }
    public void setChecked(boolean checked) { HostUi.setChecked(getPeer(), checked); }
}
