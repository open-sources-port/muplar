package com.muplar.bindersmoke;

import android.app.Activity;
import android.os.Binder;
import android.os.Bundle;
import android.os.IBinder;
import android.os.IInterface;
import android.os.Parcel;
import android.os.RemoteException;

public final class MainActivity extends Activity {

    static final class TestCallback extends Binder implements IInterface {
        static final String DESCRIPTOR = "com.muplar.test.ICallback";
        static final int TRANSACTION_TEST = IBinder.FIRST_CALL_TRANSACTION + 1;
        boolean called = false;

        TestCallback() {
            attachInterface(this, DESCRIPTOR);
        }

        @Override
        public IBinder asBinder() {
            return this;
        }

        @Override
        protected boolean onTransact(int code, Parcel data, Parcel reply, int flags)
                throws RemoteException {
            if (code == TRANSACTION_TEST) {
                data.enforceInterface(DESCRIPTOR);
                int val = data.readInt();
                if (reply != null) {
                    reply.writeNoException();
                    reply.writeInt(val * 2);
                }
                called = true;
                return true;
            }
            return super.onTransact(code, data, reply, flags);
        }
    }

    @Override
    public void onCreate(Bundle saved) {
        super.onCreate(saved);
        phase("roundtrip");
        testBasicBinderRoundTrip();
        phase("null");
        testNullBinder();
        phase("local_callback");
        testCallbackBinder();
        phase("service_binder");
        testServiceBinder();
        phase("append_and_metadata");
        testAppendAndMetadata();
        phase("lifetime");
        testParcelLifetime();
        phase("gc");
        testRepeatedAllocationAndGc();
        System.out.println("BINDER_PARCEL_SMOKE_PASS roundtrip=ok identity=ok callback=ok gc=ok");
    }

    private void testBasicBinderRoundTrip() {
        Binder binder = new Binder();
        phase("roundtrip_holder_created");
        Parcel parcel = Parcel.obtain();
        try {
            phase("roundtrip_write");
            parcel.writeStrongBinder(binder);
            phase("roundtrip_read");
            parcel.setDataPosition(0);
            IBinder read = parcel.readStrongBinder();
            require(read != null, "readStrongBinder returned null");
            require(read == binder, "binder identity mismatch: expected " + binder + " got " + read);
        } finally {
            parcel.recycle();
        }
    }

    private void testNullBinder() {
        Parcel parcel = Parcel.obtain();
        try {
            parcel.writeStrongBinder(null);
            parcel.setDataPosition(0);
            IBinder read = parcel.readStrongBinder();
            require(read == null, "expected null strong binder, got " + read);
        } finally {
            parcel.recycle();
        }
    }

    private void testCallbackBinder() {
        TestCallback callback = new TestCallback();
        Parcel parcel = Parcel.obtain();
        try {
            parcel.writeStrongBinder(callback);
            parcel.setDataPosition(0);
            IBinder read = parcel.readStrongBinder();
            require(read == callback, "callback identity mismatch");
            IInterface iface = read.queryLocalInterface(TestCallback.DESCRIPTOR);
            require(iface == callback, "queryLocalInterface mismatch");

            // Test transaction on the callback binder
            Parcel data = Parcel.obtain();
            Parcel reply = Parcel.obtain();
            try {
                data.writeInterfaceToken(TestCallback.DESCRIPTOR);
                data.writeInt(21);
                boolean success = read.transact(TestCallback.TRANSACTION_TEST, data, reply, 0);
                require(success, "transact returned false");
                require(callback.called, "callback onTransact was not invoked");
                reply.readException();
                int result = reply.readInt();
                require(result == 42, "unexpected transaction result: " + result);
            } catch (RemoteException e) {
                throw new AssertionError(e);
            } finally {
                data.recycle();
                reply.recycle();
            }

            // Local Binder death registration is a no-op; remote death delivery
            // requires a separate cross-process test.
            IBinder.DeathRecipient recipient = new IBinder.DeathRecipient() {
                @Override
                public void binderDied() {
                }
            };
            try {
                read.linkToDeath(recipient, 0);
                boolean unlinked = read.unlinkToDeath(recipient, 0);
                require(unlinked, "unlinkToDeath returned false");
            } catch (RemoteException e) {
                throw new AssertionError(e);
            }
        } finally {
            parcel.recycle();
        }
    }

    private void testServiceBinder() {
        try {
            Class<?> manager = Class.forName("android.os.ServiceManager");
            IBinder service = (IBinder) manager.getMethod("getService", String.class)
                .invoke(null, "muplar.test.unsupported");
            require(service instanceof Binder, "service does not have a genuine local Binder");
            Parcel data = Parcel.obtain(service), reply = Parcel.obtain();
            try {
                data.writeInterfaceToken("muplar.test.unsupported");
                require(service.transact(IBinder.FIRST_CALL_TRANSACTION, data, reply, 0),
                    "unsupported service did not dispatch its error");
                try { reply.readException(); throw new AssertionError("service fabricated success"); }
                catch (UnsupportedOperationException expected) {}
            } finally { data.recycle(); reply.recycle(); }
            IBinder network = (IBinder) manager.getMethod("getService", String.class)
                .invoke(null, "network_management");
            require(network instanceof Binder, "network service is not a native Binder");
            Object local = Class.forName("android.os.INetworkManagementService$Stub")
                .getMethod("asInterface", IBinder.class).invoke(null, network);
            try {
                Class.forName("android.os.INetworkManagementService")
                    .getMethod("isBandwidthControlEnabled").invoke(local);
                throw new AssertionError("network service fabricated bandwidth control state");
            } catch (java.lang.reflect.InvocationTargetException expected) {
                require(expected.getCause() instanceof UnsupportedOperationException,
                    "network service failed with wrong error");
            }
        } catch (ReflectiveOperationException | RemoteException e) { throw new AssertionError(e); }
    }

    private static boolean hasBinders(Parcel parcel) {
        try {
            java.lang.reflect.Method method = Parcel.class.getDeclaredMethod("hasBinders");
            method.setAccessible(true);
            return (Boolean) method.invoke(parcel);
        } catch (ReflectiveOperationException e) { throw new AssertionError(e); }
    }

    private void testAppendAndMetadata() {
        Binder binder = new Binder();
        Parcel source = Parcel.obtain(), copy = Parcel.obtain();
        try {
            source.writeStrongBinder(binder);
            require(hasBinders(source), "Binder object metadata missing");
            try { source.marshall(); throw new AssertionError("Binder parcel marshalled as bytes"); }
            catch (RuntimeException expected) {}
            copy.appendFrom(source, 0, source.dataSize());
            source.recycle();
            source = null;
            copy.setDataPosition(0);
            require(copy.readStrongBinder() == binder, "append lost Binder identity/ownership");
            copy.setDataSize(0);
            require(!hasBinders(copy), "truncation retained Binder references");
        } finally {
            if (source != null) source.recycle();
            copy.recycle();
        }
        Parcel malformed = Parcel.obtain();
        try {
            malformed.writeInt(1); malformed.writeLong(1234); malformed.writeInt(0);
            malformed.setDataPosition(0);
            try { malformed.readStrongBinder(); throw new AssertionError("raw bytes forged a Binder"); }
            catch (android.os.BadParcelableException expected) {}
        } finally { malformed.recycle(); }
        TestCallback callback = new TestCallback();
        Parcel data = Parcel.obtain(), reply = Parcel.obtain();
        try {
            data.writeInterfaceToken("wrong.interface");
            data.writeInt(21);
            try {
                callback.transact(TestCallback.TRANSACTION_TEST, data, reply, 0);
                throw new AssertionError("wrong interface accepted");
            } catch (SecurityException expected) {}
            catch (RemoteException e) { throw new AssertionError(e); }
        } finally { data.recycle(); reply.recycle(); }
    }

    private static java.lang.ref.WeakReference<Binder> writeWeakBinder(
            Parcel parcel, java.lang.ref.ReferenceQueue<Binder> queue) {
        Binder binder = new Binder();
        parcel.writeStrongBinder(binder);
        return new java.lang.ref.WeakReference<>(binder, queue);
    }

    private static void requireRetained(java.lang.ref.WeakReference<Binder> weak) {
        require(weak.get() != null, "parcel did not retain its Binder");
    }

    private void testParcelLifetime() {
        Parcel parcel = Parcel.obtain();
        java.lang.ref.ReferenceQueue<Binder> queue = new java.lang.ref.ReferenceQueue<>();
        java.lang.ref.WeakReference<Binder> weak = writeWeakBinder(parcel, queue);
        System.gc();
        requireRetained(weak);
        parcel.recycle();
        // Poll the reference queue: weak.get() in the loop would itself keep
        // the referent live in an ART interpreter register during the next GC.
        for (int i = 0; i < 40; ++i) {
            System.gc();
            System.runFinalization();
            if (queue.poll() == weak) return;
            try { Thread.sleep(50); } catch (InterruptedException e) { throw new AssertionError(e); }
        }
        throw new AssertionError("recycled parcel leaked its Binder");
    }

    private void testRepeatedAllocationAndGc() {
        for (int i = 0; i < 500; i++) {
            Binder b = new Binder();
            Parcel p = Parcel.obtain();
            try {
                p.writeStrongBinder(b);
                p.setDataPosition(0);
                IBinder read = p.readStrongBinder();
                require(read == b, "loop identity mismatch at " + i);
            } finally {
                p.recycle();
            }
            if (i % 100 == 0) {
                System.gc();
                System.runFinalization();
            }
        }
        System.gc();
        System.runFinalization();
    }

    private static void require(boolean condition, String message) {
        if (!condition) {
            throw new AssertionError(message);
        }
    }

    private static void phase(String name) {
        System.out.println("BINDER_PARCEL_SMOKE_PHASE " + name);
        System.out.flush();
    }
}
