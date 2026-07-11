package android.app.admin;

import android.os.IInterface;
import android.os.RemoteException;

public interface IDevicePolicyManager extends IInterface {
    ParcelableResource getString(String stringId)
        throws RemoteException;

    ManagedSubscriptionsPolicy getManagedSubscriptionsPolicy()
        throws RemoteException;
}
