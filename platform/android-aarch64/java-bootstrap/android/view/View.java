package android.view;

import com.muplar.runtime.HostUi;

public class View {
    public interface OnClickListener { void onClick(View view); }
    private final Object peer;
    private int id;

    protected View(Object peer) { this.peer = peer; }
    public final Object getPeer() { return peer; }
    public void setEnabled(boolean enabled) { HostUi.setEnabled(peer, enabled); }
    public void setId(int id) { this.id = id; }
    public int getId() { return id; }
    public View findViewById(int wantedId) {
        return id == wantedId ? this : null;
    }
}
