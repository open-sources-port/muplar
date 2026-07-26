package android.view;

import android.graphics.Rect;
import android.os.Binder;
import android.os.Bundle;
import android.os.IBinder;
import android.os.IInterface;
import android.os.RemoteException;
import android.window.TaskSnapshot;

public interface IRecentsAnimationRunner extends IInterface {
    void onAnimationStart(IRecentsAnimationController controller,
                          RemoteAnimationTarget[] apps,
                          RemoteAnimationTarget[] wallpapers,
                          Rect homeContentInsets,
                          Rect minimizedHomeBounds,
                          Bundle extras) throws RemoteException;

    void onAnimationCanceled(int[] taskIds,
                             TaskSnapshot[] taskSnapshots) throws RemoteException;

    void onTasksAppeared(RemoteAnimationTarget[] apps) throws RemoteException;

    abstract class Stub extends Binder implements IRecentsAnimationRunner {
        public Stub() {
        }

        public static IRecentsAnimationRunner asInterface(IBinder binder) {
            if (binder instanceof IRecentsAnimationRunner) {
                return (IRecentsAnimationRunner) binder;
            }
            return null;
        }

        @Override
        public IBinder asBinder() {
            return this;
        }
    }
}
