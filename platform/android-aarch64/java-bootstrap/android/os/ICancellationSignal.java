package android.os;

public interface ICancellationSignal extends IInterface {
    void cancel() throws RemoteException;

    abstract class Stub extends Binder implements ICancellationSignal {
        public Stub() {
        }

        public static ICancellationSignal asInterface(IBinder binder) {
            if (binder instanceof ICancellationSignal) {
                return (ICancellationSignal) binder;
            }
            return null;
        }

        @Override
        public IBinder asBinder() {
            return this;
        }
    }
}
