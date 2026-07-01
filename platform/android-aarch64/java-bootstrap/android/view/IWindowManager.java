package android.view;

import android.os.IInterface;
import android.os.RemoteException;

public interface IWindowManager extends IInterface {
    void registerSystemGestureExclusionListener(
        ISystemGestureExclusionListener listener, int displayId) throws RemoteException;
    void unregisterSystemGestureExclusionListener(
        ISystemGestureExclusionListener listener, int displayId) throws RemoteException;
}
