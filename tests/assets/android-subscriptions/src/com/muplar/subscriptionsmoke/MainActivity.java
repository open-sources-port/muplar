package com.muplar.subscriptionsmoke;

import android.app.Activity;
import android.content.Context;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.telephony.SubscriptionManager;
import java.util.concurrent.Executor;

public final class MainActivity extends Activity {
    @Override
    public void onCreate(Bundle saved) {
        super.onCreate(saved);
        SubscriptionManager manager = getSystemService(SubscriptionManager.class);
        require(manager != null, "typed lookup missing");
        require(manager == getSystemService(Context.TELEPHONY_SUBSCRIPTION_SERVICE), "lookup identity");
        require(manager.getActiveSubscriptionInfoList().isEmpty(), "active subscriptions invented");
        require(manager.getActiveSubscriptionInfoCount() == 0, "nonzero active count");
        require(manager.getActiveSubscriptionInfoCountMax() == 0, "modem capacity invented");
        require(manager.getActiveSubscriptionInfo(1) == null, "subscription invented");
        require(manager.getActiveSubscriptionInfoForSimSlotIndex(0) == null, "SIM invented");
        require(SubscriptionManager.getDefaultSubscriptionId() == -1, "default ID valid without SIM");
        require(SubscriptionManager.getDefaultDataSubscriptionId() == -1, "data ID valid without SIM");
        require(SubscriptionManager.getDefaultVoiceSubscriptionId() == -1, "voice ID valid without SIM");
        require(SubscriptionManager.getDefaultSmsSubscriptionId() == -1, "SMS ID valid without SIM");
        require(activeIds(manager).length == 0, "active IDs invented");

        verifyMutationRejected();

        final int[] directCalls = {0};
        Executor direct = Runnable::run;
        SubscriptionManager.OnSubscriptionsChangedListener directListener =
            new SubscriptionManager.OnSubscriptionsChangedListener() {
                @Override public void onSubscriptionsChanged() { directCalls[0]++; }
            };
        manager.addOnSubscriptionsChangedListener(direct, directListener);
        require(directCalls[0] == 1, "initial executor callback missing");
        manager.addOnSubscriptionsChangedListener(direct, directListener);
        require(directCalls[0] == 1, "duplicate registration notified twice");
        manager.removeOnSubscriptionsChangedListener(directListener);
        manager.removeOnSubscriptionsChangedListener(directListener);
        manager.addOnSubscriptionsChangedListener(direct, directListener);
        require(directCalls[0] == 2, "re-registration missing");
        manager.removeOnSubscriptionsChangedListener(directListener);

        final int[] legacyCalls = {0};
        SubscriptionManager.OnSubscriptionsChangedListener legacy =
            new SubscriptionManager.OnSubscriptionsChangedListener() {
                @Override public void onSubscriptionsChanged() {
                    require(Looper.myLooper() == Looper.getMainLooper(), "wrong legacy callback thread");
                    legacyCalls[0]++;
                }
            };
        manager.addOnSubscriptionsChangedListener(legacy);
        new Handler(Looper.getMainLooper()).post(() -> {
            require(legacyCalls[0] == 1, "initial legacy callback missing");
            require(directCalls[0] == 2, "removed listener was notified");
            manager.removeOnSubscriptionsChangedListener(legacy);
            System.out.println("SUBSCRIPTION_SMOKE_PASS queries=empty ids=invalid callbacks=3 lifecycle=ok");
        });
    }

    private static void verifyMutationRejected() {
        try {
            android.os.IBinder binder = (android.os.IBinder) Class.forName("android.os.ServiceManager")
                .getMethod("getService", String.class).invoke(null, "isub");
            Object service = binder.queryLocalInterface("com.android.internal.telephony.ISub");
            require(service != null, "subscription service is not local");
            Class.forName("com.android.internal.telephony.ISub")
                .getMethod("setDefaultDataSubId", int.class).invoke(service, 1);
            throw new AssertionError("unsupported mutation reported success");
        } catch (java.lang.reflect.InvocationTargetException e) {
            require(e.getCause() instanceof UnsupportedOperationException,
                "wrong mutation error: " + e.getCause());
        } catch (ReflectiveOperationException e) {
            throw new AssertionError(e);
        }
    }

    private static int[] activeIds(SubscriptionManager manager) {
        try {
            return (int[]) SubscriptionManager.class.getMethod("getActiveSubscriptionIdList")
                .invoke(manager);
        } catch (ReflectiveOperationException e) {
            throw new AssertionError(e);
        }
    }

    private static void require(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
}
