package android.view;

import android.os.Binder;
import android.os.IBinder;
import android.os.IInterface;

public interface IRecentsAnimationController extends IInterface {
    abstract class Stub extends Binder implements IRecentsAnimationController {
        public Stub() {
        }

        public static IRecentsAnimationController asInterface(IBinder binder) {
            if (binder instanceof IRecentsAnimationController) {
                return (IRecentsAnimationController) binder;
            }
            return null;
        }

        @Override
        public IBinder asBinder() {
            return this;
        }
    }
}
