package android.os;

public class Binder implements IBinder {
    private IInterface owner;
    private String descriptor = "";

    public void attachInterface(IInterface owner, String descriptor) {
        this.owner = owner;
        this.descriptor = descriptor == null ? "" : descriptor;
    }
    public String getInterfaceDescriptor() { return descriptor; }
    public boolean pingBinder() { return true; }
    public boolean isBinderAlive() { return true; }
    public IInterface queryLocalInterface(String requested) {
        return descriptor.equals(requested) ? owner : null;
    }
    public boolean transact(int code, Parcel data, Parcel reply, int flags)
        throws RemoteException {
        return onTransact(code, data, reply, flags);
    }
    protected boolean onTransact(int code, Parcel data, Parcel reply, int flags)
        throws RemoteException {
        if (code == INTERFACE_TRANSACTION && reply != null) {
            reply.writeString(descriptor);
            return true;
        }
        return false;
    }
    public IBinder asBinder() { return this; }
    public static int getCallingPid() { return 1; }
    public static int getCallingUid() { return 10000; }
}
