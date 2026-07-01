import android.os.IBinder;
import android.os.Parcel;
import android.os.ServiceManager;

public final class JavaBinderSmoke {
    public static void main(String[] args) throws Exception {
        IBinder binder = ServiceManager.getService("muplar.test.java.echo");
        if (binder == null || !binder.isBinderAlive())
            throw new AssertionError("daemon Binder service missing");
        Parcel input = Parcel.obtain();
        Parcel output = Parcel.obtain();
        input.writeInterfaceToken("com.muplar.test.IEcho");
        input.writeInt(91);
        if (!binder.transact(IBinder.FIRST_CALL_TRANSACTION + 4,
                             input, output, 0))
            throw new AssertionError("transaction rejected");
        if (output.readInt() != 91)
            throw new AssertionError("parcel reply mismatch");
        System.out.println("javaBinderReply=91");
    }
}
