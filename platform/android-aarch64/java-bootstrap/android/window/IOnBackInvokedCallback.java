package android.window;

import android.os.Binder;
import android.os.IBinder;
import android.os.IInterface;

public interface IOnBackInvokedCallback extends IInterface {
    void onBackStarted(BackMotionEvent event);
    void onBackProgressed(BackMotionEvent event);
    void onBackCancelled();
    void onBackInvoked();
    abstract class Stub extends Binder implements IOnBackInvokedCallback {
        @Override public IBinder asBinder() { return this; }
    }
}
