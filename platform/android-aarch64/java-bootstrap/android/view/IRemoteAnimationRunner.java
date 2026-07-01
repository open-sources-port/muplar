package android.view;

import android.os.Binder;
import android.os.IBinder;
import android.os.IInterface;
import android.os.RemoteException;

public interface IRemoteAnimationRunner extends IInterface {
    void onAnimationStart(int transit, RemoteAnimationTarget[] apps,
        RemoteAnimationTarget[] wallpapers, RemoteAnimationTarget[] nonApps,
        IRemoteAnimationFinishedCallback finishedCallback) throws RemoteException;
    void onAnimationCancelled() throws RemoteException;
    abstract class Stub extends Binder implements IRemoteAnimationRunner {
        @Override public IBinder asBinder() { return this; }
    }
}
