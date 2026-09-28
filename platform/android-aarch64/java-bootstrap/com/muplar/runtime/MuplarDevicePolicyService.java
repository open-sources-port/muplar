package com.muplar.runtime;

import android.app.admin.IDevicePolicyManager;
import android.app.admin.ManagedSubscriptionsPolicy;
import android.app.admin.ParcelableResource;
import android.os.Binder;
import android.os.IBinder;

/** Local, unmanaged-device defaults without entering kernel Binder. */
final class MuplarDevicePolicyService extends IDevicePolicyManager.Default {
    private final Binder binder = new Binder();

    MuplarDevicePolicyService() {
        binder.attachInterface(this, "android.app.admin.IDevicePolicyManager");
    }

    @Override public IBinder asBinder() {
        return binder;
    }

    @Override public ParcelableResource getString(String stringId) {
        return null; // DevicePolicyResourcesManager uses its resource fallback.
    }

    @Override public ManagedSubscriptionsPolicy getManagedSubscriptionsPolicy() {
        return new ManagedSubscriptionsPolicy(
            ManagedSubscriptionsPolicy.TYPE_ALL_PERSONAL_SUBSCRIPTIONS);
    }
}
