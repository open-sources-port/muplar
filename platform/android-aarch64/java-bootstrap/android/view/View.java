package android.view;

import com.muplar.runtime.HostUi;

public class View {
    public interface OnClickListener { void onClick(View view); }
    private final Object peer;

    protected View(Object peer) { this.peer = peer; }
    public final Object getPeer() { return peer; }
    public void setEnabled(boolean enabled) { HostUi.setEnabled(peer, enabled); }
}
