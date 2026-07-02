package android.os;

public interface IRemoteCallback extends IInterface {
    void sendResult(Bundle data) throws RemoteException;

    abstract class Stub extends Binder implements IRemoteCallback {
        private static final String DESCRIPTOR = "android.os.IRemoteCallback";

        public Stub() { attachInterface(this, DESCRIPTOR); }

        public static IRemoteCallback asInterface(IBinder binder) {
            if (binder == null) return null;
            IInterface local = binder.queryLocalInterface(DESCRIPTOR);
            return local instanceof IRemoteCallback ? (IRemoteCallback) local : null;
        }

        @Override public IBinder asBinder() { return this; }
    }
}
