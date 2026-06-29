package android.widget;

import android.content.Context;
import com.muplar.runtime.HostUi;

public class CheckBox extends Button {
    public CheckBox(Context context) { super(context, HostUi.createCheckBox()); }
    public boolean isChecked() { return HostUi.isChecked(getPeer()); }
    public void setChecked(boolean checked) { HostUi.setChecked(getPeer(), checked); }
}
