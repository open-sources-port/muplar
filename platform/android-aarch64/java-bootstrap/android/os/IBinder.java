package android.os;

public interface IBinder {
    interface DeathRecipient { void binderDied(); }
    int FIRST_CALL_TRANSACTION = 1;
    int LAST_CALL_TRANSACTION = 0x00ffffff;
    int FLAG_ONEWAY = 1;
    int INTERFACE_TRANSACTION = 0x5f4e5446;

    String getInterfaceDescriptor() throws RemoteException;
    boolean pingBinder();
    boolean isBinderAlive();
    IInterface queryLocalInterface(String descriptor);
    boolean transact(int code, Parcel data, Parcel reply, int flags)
        throws RemoteException;
    default void linkToDeath(DeathRecipient recipient, int flags)
            throws RemoteException {}
    default boolean unlinkToDeath(DeathRecipient recipient, int flags) {
        return true;
    }
}
