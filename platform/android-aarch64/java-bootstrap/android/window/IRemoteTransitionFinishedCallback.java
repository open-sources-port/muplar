package android.window;

import android.os.Binder;
import android.os.IBinder;
import android.os.IInterface;
import android.os.RemoteException;
import android.view.SurfaceControl;

public interface IRemoteTransitionFinishedCallback extends IInterface {
    void onTransitionFinished(WindowContainerTransaction transaction,
        SurfaceControl.Transaction surfaceTransaction) throws RemoteException;
    abstract class Stub extends Binder implements IRemoteTransitionFinishedCallback {
        @Override public IBinder asBinder() { return this; }
    }
}
