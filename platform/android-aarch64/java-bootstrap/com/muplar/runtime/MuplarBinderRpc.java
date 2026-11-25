package com.muplar.runtime;

import android.os.IBinder;
import android.os.RemoteException;

/** Experimental socket-backed Binder endpoints within one private Muplar instance. */
public final class MuplarBinderRpc {
    private MuplarBinderRpc() {}
    public static native long startServer(String socket, IBinder root) throws RemoteException;
    public static native IBinder connect(String socket) throws RemoteException;
    public static native void stopServer(long server);
}
