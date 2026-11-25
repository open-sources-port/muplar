package com.muplar.runtime;

import java.lang.reflect.Method;
import java.util.ArrayList;

/** Immutable no-modem/no-SIM subscription state for the local framework bridge. */
final class MuplarSubscriptionService {
    private MuplarSubscriptionService() {}

    static synchronized void installFramework() {
        try {
            Class<?> initializer = Class.forName("android.telephony.TelephonyFrameworkInitializer");
            if (initializer.getMethod("getTelephonyServiceManager").invoke(null) == null) {
                Class<?> manager = Class.forName("android.os.TelephonyServiceManager");
                initializer.getMethod("setTelephonyServiceManager", manager)
                    .invoke(null, manager.getDeclaredConstructor().newInstance());
            }
        } catch (ReflectiveOperationException e) {
            throw new IllegalStateException("Cannot initialize telephony framework", e);
        }
    }

    static Object query(Method method) {
        String name = method.getName();
        switch (name) {
            case "getActiveSubscriptionInfoList":
            case "getAllSubInfoList":
            case "getAvailableSubscriptionInfoList":
            case "getAccessibleSubscriptionInfoList":
            case "getOpportunisticSubscriptions":
            case "getSubscriptionsInGroup":
            case "getSubscriptionInfoListAssociatedWithUser":
                return new ArrayList<Object>();
            case "getActiveSubIdList":
                return new int[0];
            case "getActiveSubInfoCount":
            case "getActiveSubInfoCountMax":
                return Integer.valueOf(0);
            case "getDefaultSubId":
            case "getDefaultSubIdAsUser":
            case "getDefaultDataSubId":
            case "getDefaultVoiceSubId":
            case "getDefaultVoiceSubIdAsUser":
            case "getDefaultSmsSubId":
            case "getDefaultSmsSubIdAsUser":
            case "getActiveDataSubscriptionId":
            case "getPreferredDataSubscriptionId":
            case "getEnabledSubscriptionId":
            case "getSlotIndex":
            case "getPhoneId":
                return Integer.valueOf(-1);
            case "getSubId":
                return method.getReturnType().isArray() ? new int[0] : Integer.valueOf(-1);
            case "isActiveSubId":
            case "isSubscriptionEnabled":
            case "isSubscriptionAssociatedWithCallingUser":
            case "isSubscriptionAssociatedWithUser":
            case "canDisablePhysicalSubscription":
                return Boolean.FALSE;
            case "getActiveSubscriptionInfo":
            case "getActiveSubscriptionInfoForIccId":
            case "getActiveSubscriptionInfoForSimSlotIndex":
            case "getSubscriptionProperty":
            case "getSubscriptionUserHandle":
                return null;
            case "getPhoneNumber":
            case "getPhoneNumberFromFirstAvailableSource":
                return "";
            default:
                throw new UnsupportedOperationException("No subscription service operation: " + name);
        }
    }

    static Object registry(Method method, Object[] args) throws Exception {
        String name = method.getName();
        if ("addOnSubscriptionsChangedListener".equals(name) ||
                "addOnOpportunisticSubscriptionsChangedListener".equals(name)) {
            // The real TelephonyRegistryManager owns registration identity,
            // duplicate suppression, executor dispatch and removal. Our state is
            // already initialized and never changes, so deliver the initial
            // snapshot and retain no additional callback references here.
            for (Object arg : args) {
                if (arg instanceof android.os.IInterface) {
                    Method changed = Class.forName(
                        "com.android.internal.telephony.IOnSubscriptionsChangedListener")
                        .getMethod("onSubscriptionsChanged");
                    changed.invoke(arg);
                    return null;
                }
            }
            throw new IllegalArgumentException("Subscription callback missing");
        }
        if ("removeOnSubscriptionsChangedListener".equals(name)) {
            return null;
        }
        throw new UnsupportedOperationException("No telephony registry operation: " + name);
    }
}
