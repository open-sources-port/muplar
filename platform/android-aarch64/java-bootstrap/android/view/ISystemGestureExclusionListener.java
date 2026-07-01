package android.view;

import android.graphics.Region;
import android.os.Binder;
import android.os.IBinder;
import android.os.IInterface;

public interface ISystemGestureExclusionListener extends IInterface {
    void onSystemGestureExclusionChanged(int displayId, Region exclusionRegion,
        Region unrestrictedRegion);

    abstract class Stub extends Binder implements ISystemGestureExclusionListener {
        public Stub() {
            attachInterface(this, "android.view.ISystemGestureExclusionListener");
        }
        @Override public IBinder asBinder() { return this; }
    }
}
