package android.app.admin;

import android.os.IInterface;
import android.os.RemoteException;

public interface IDevicePolicyManager extends IInterface {
    // Compile-time surface. The framework's generated Default supplies the
    // remaining vendor methods at runtime through parent-first class loading.
    abstract class Default implements IDevicePolicyManager {
    }

    ParcelableResource getString(String stringId)
        throws RemoteException;

    ManagedSubscriptionsPolicy getManagedSubscriptionsPolicy()
        throws RemoteException;
}
