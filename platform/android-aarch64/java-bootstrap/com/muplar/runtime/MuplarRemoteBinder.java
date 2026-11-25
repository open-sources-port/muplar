package com.muplar.runtime;

import android.os.IBinder;
import android.os.IInterface;
import android.os.Parcel;
import android.os.RemoteException;
import java.io.FileDescriptor;
import java.util.concurrent.Executor;

/** Proxy owning a genuine native libbinder RPC strong reference. */
final class MuplarRemoteBinder implements IBinder {
    private long handle;
    private MuplarRemoteBinder(long handle) { this.handle = handle; }
    private static native void release(long handle);
    private static native boolean transact(long handle, int code, Parcel data, Parcel reply, int flags)
        throws RemoteException;
    private static native void link(long handle, DeathRecipient recipient, int flags) throws RemoteException;
    private static native boolean unlink(long handle, DeathRecipient recipient, int flags);
    private static native boolean alive(long handle, boolean ping);
    private static native String descriptor(long handle);
    @Override public String getInterfaceDescriptor() { return descriptor(handle); }
    @Override public boolean pingBinder() { return alive(handle, true); }
    @Override public boolean isBinderAlive() { return alive(handle, false); }
    @Override public IInterface queryLocalInterface(String descriptor) { return null; }
    @Override public boolean transact(int code, Parcel data, Parcel reply, int flags) throws RemoteException {
        if (data == null) throw new NullPointerException("data");
        return transact(handle, code, data, reply, flags);
    }
    @Override public void dump(FileDescriptor fd, String[] args) {
        throw new UnsupportedOperationException("RPC dump not implemented");
    }
    @Override public void dumpAsync(FileDescriptor fd, String[] args) { dump(fd, args); }
    @Override public void linkToDeath(DeathRecipient recipient, int flags) throws RemoteException {
        if (recipient == null) throw new NullPointerException("recipient");
        link(handle, recipient, flags);
    }
    @Override public boolean unlinkToDeath(DeathRecipient recipient, int flags) {
        if (recipient == null) throw new NullPointerException("recipient");
        return unlink(handle, recipient, flags);
    }
    @Override public void addFrozenStateChangeCallback(Executor executor, FrozenStateChangeCallback callback) {
        throw new UnsupportedOperationException("RPC freezing not implemented");
    }
    @Override public boolean removeFrozenStateChangeCallback(FrozenStateChangeCallback callback) {
        throw new UnsupportedOperationException("RPC freezing not implemented");
    }
    @Override protected void finalize() throws Throwable {
        try { long owned = handle; handle = 0; release(owned); }
        finally { super.finalize(); }
    }
}
