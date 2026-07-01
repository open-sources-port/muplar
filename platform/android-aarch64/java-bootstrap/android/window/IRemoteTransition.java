package android.window;

import android.os.Binder;
import android.os.IBinder;
import android.os.IInterface;
import android.os.RemoteException;
import android.view.SurfaceControl;

public interface IRemoteTransition extends IInterface {
    void startAnimation(IBinder transition, TransitionInfo info,
        SurfaceControl.Transaction transaction,
        IRemoteTransitionFinishedCallback finishCallback) throws RemoteException;
    void mergeAnimation(IBinder transition, TransitionInfo info,
        SurfaceControl.Transaction transaction, IBinder mergeTarget,
        IRemoteTransitionFinishedCallback finishCallback) throws RemoteException;
    abstract class Stub extends Binder implements IRemoteTransition {
        @Override public IBinder asBinder() { return this; }
    }
}
