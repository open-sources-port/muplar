package android.view;

import android.os.Binder;
import android.os.IBinder;
import android.os.IInterface;
import android.os.RemoteException;

public interface IRemoteAnimationFinishedCallback extends IInterface {
    void onAnimationFinished() throws RemoteException;
    abstract class Stub extends Binder implements IRemoteAnimationFinishedCallback {
        @Override public IBinder asBinder() { return this; }
    }
}
