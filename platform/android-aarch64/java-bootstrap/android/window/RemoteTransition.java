package android.window;

import android.os.Parcel;
import android.os.Parcelable;
import android.app.IApplicationThread;

public final class RemoteTransition implements Parcelable {
    private String debugName;
    public RemoteTransition() {}
    public RemoteTransition(IRemoteTransition remoteTransition,
            IApplicationThread appThread, String debugName) {
        this.debugName = debugName;
    }
    public RemoteTransition setDebugName(String name) {
        debugName = name;
        return this;
    }
    public String getDebugName() { return debugName; }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel destination, int flags) {
        destination.writeString(debugName);
    }
}
