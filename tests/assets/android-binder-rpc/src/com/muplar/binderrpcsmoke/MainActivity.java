package com.muplar.binderrpcsmoke;

import android.app.Activity;
import android.os.Binder;
import android.os.Bundle;
import android.os.IBinder;
import android.os.Parcel;
import android.os.RemoteException;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

public final class MainActivity extends Activity {
    private static final String NAME = "com.muplar.test.IRpc";
    private static final String SOCKET = "/tmp/brpc-test.sock";
    private static final int ECHO = IBinder.FIRST_CALL_TRANSACTION;
    private static final int CALLBACK = ECHO + 1;
    private static final int ERROR = ECHO + 2;
    private static final int MISSING = ECHO + 3;
    private static final int IDENTITY = ECHO + 4;

    private static final class Endpoint extends Binder {
        Endpoint() { attachInterface(null, NAME); }
        @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags)
                throws RemoteException {
            if (code == IBinder.INTERFACE_TRANSACTION) { reply.writeString(NAME); return true; }
            if (code == MISSING) return false;
            data.enforceInterface(NAME);
            if (code == IDENTITY) {
                reply.writeNoException(); reply.writeStrongBinder(this); reply.writeStrongBinder(this); return true;
            }
            if (code == ERROR) throw new SecurityException("RPC deliberate denial");
            if (code == ECHO) {
                int value = data.readInt();
                reply.writeNoException();
                reply.writeInt(value * 2);
                return true;
            }
            if (code == CALLBACK) {
                IBinder callback = data.readStrongBinder();
                require(callback != null, "remote callback missing");
                int result = invoke(callback, ECHO, 21, null);
                reply.writeNoException();
                reply.writeInt(result);
                return true;
            }
            return false;
        }
    }

    private static int invoke(IBinder target, int code, int value, IBinder callback) throws RemoteException {
        Parcel data = Parcel.obtain(target), reply = Parcel.obtain();
        try {
            data.writeInterfaceToken(NAME);
            if (callback == null) data.writeInt(value);
            else data.writeStrongBinder(callback);
            boolean success = target.transact(code, data, reply, 0);
            if (code == MISSING) { require(!success, "unknown transaction reported success"); return 0; }
            require(success, "remote transaction failed");
            reply.readException();
            return reply.readInt();
        } finally { data.recycle(); reply.recycle(); }
    }

    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        new Thread(() -> {
            try {
                Class<?> api = Class.forName("com.muplar.runtime.MuplarBinderRpc");
                if (getPackageName().endsWith(".server")) {
                    long server = (Long) api.getMethod("startServer", String.class, IBinder.class)
                        .invoke(null, SOCKET, new Endpoint());
                    require(server != 0, "server not started");
                    System.out.println("BINDER_RPC_SERVER_READY");
                    return;
                }
                IBinder remote = (IBinder) api.getMethod("connect", String.class).invoke(null, SOCKET);
                require(remote != null && remote.pingBinder(), "remote not alive");
                require(NAME.equals(remote.getInterfaceDescriptor()), "remote descriptor wrong");
                require(invoke(remote, ECHO, 21, null) == 42, "remote result wrong");
                require(invoke(remote, CALLBACK, 0, new Endpoint()) == 42, "cross-process callback wrong");
                Parcel identityData = Parcel.obtain(remote), identityReply = Parcel.obtain();
                try {
                    identityData.writeInterfaceToken(NAME);
                    require(remote.transact(IDENTITY, identityData, identityReply, 0), "identity transaction failed");
                    identityReply.readException();
                    require(identityReply.readStrongBinder() == remote, "remote proxy identity changed");
                    require(identityReply.readStrongBinder() == remote, "repeated remote proxy identity changed");
                } finally { identityData.recycle(); identityReply.recycle(); }
                invoke(remote, MISSING, 0, null);
                try { invoke(remote, ERROR, 0, null); throw new AssertionError("remote exception lost"); }
                catch (SecurityException expected) {}
                CountDownLatch died = new CountDownLatch(1);
                IBinder.DeathRecipient removed = () -> { throw new AssertionError("unlinked recipient invoked"); };
                remote.linkToDeath(removed, 0);
                require(remote.unlinkToDeath(removed, 0), "remote unlink failed");
                remote.linkToDeath(died::countDown, 0);
                System.out.println("BINDER_RPC_TRANSACTIONS_PASS callback=remote exception=preserved unknown=false");
                require(died.await(20, TimeUnit.SECONDS), "remote death notification missing");
                require(!remote.isBinderAlive(), "dead remote reported alive");
                try { invoke(remote, ECHO, 1, null); throw new AssertionError("dead transaction succeeded"); }
                catch (RemoteException expected) {}
                System.out.println("BINDER_RPC_SMOKE_PASS death=delivered dead_transaction=rejected");
            } catch (Throwable e) {
                e.printStackTrace();
                System.out.println("BINDER_RPC_SMOKE_FAIL " + e);
            }
        }, "binder-rpc-test").start();
    }

    private static void require(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
}
