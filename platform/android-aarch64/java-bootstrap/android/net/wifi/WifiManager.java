package android.net.wifi;

import android.content.Context;

public class WifiManager {
    public static final String ACTION_PICK_WIFI_NETWORK = "android.net.wifi.PICK_WIFI_NETWORK";
    public static final String EXTRA_PREVIOUS_WIFI_STATE = "previous_wifi_state";
    public static final String EXTRA_WIFI_STATE = "wifi_state";
    public static final String NETWORK_IDS_CHANGED_ACTION = "android.net.wifi.NETWORK_IDS_CHANGED";
    public static final String NETWORK_STATE_CHANGED_ACTION = "android.net.wifi.STATE_CHANGE";
    public static final String RSSI_CHANGED_ACTION = "android.net.wifi.RSSI_CHANGED";
    public static final String SCAN_RESULTS_AVAILABLE_ACTION = "android.net.wifi.SCAN_RESULTS";
    public static final String SUPPLICANT_CONNECTION_CHANGE_ACTION = "android.net.wifi.supplicant.CONNECTION_CHANGE";
    public static final String SUPPLICANT_STATE_CHANGED_ACTION = "android.net.wifi.supplicant.STATE_CHANGE";
    public static final String WIFI_STATE_CHANGED_ACTION = "android.net.wifi.WIFI_STATE_CHANGED";

    public static final int WIFI_STATE_DISABLING = 0;
    public static final int WIFI_STATE_DISABLED = 1;
    public static final int WIFI_STATE_ENABLING = 2;
    public static final int WIFI_STATE_ENABLED = 3;
    public static final int WIFI_STATE_UNKNOWN = 4;

    public static final int WIFI_MODE_FULL = 1;
    public static final int WIFI_MODE_SCAN_ONLY = 2;
    public static final int WIFI_MODE_FULL_HIGH_PERF = 3;
    public static final int WIFI_MODE_FULL_LOW_LATENCY = 4;

    private final Context context;

    public class WifiLock {
        private boolean held;

        public void acquire() {
            held = true;
        }

        public void release() {
            held = false;
        }

        public boolean isHeld() {
            return held;
        }

        public void setReferenceCounted(boolean refCounted) {
        }
    }

    public class MulticastLock {
        private boolean held;

        public void acquire() {
            held = true;
        }

        public void release() {
            held = false;
        }

        public boolean isHeld() {
            return held;
        }

        public void setReferenceCounted(boolean refCounted) {
        }
    }

    public WifiManager(Context context) {
        this.context = context;
    }

    public boolean isWifiEnabled() {
        return true;
    }

    public boolean setWifiEnabled(boolean enabled) {
        return true;
    }

    public int getWifiState() {
        return WIFI_STATE_ENABLED;
    }

    public WifiInfo getConnectionInfo() {
        return new WifiInfo();
    }

    public android.net.DhcpInfo getDhcpInfo() {
        return new android.net.DhcpInfo();
    }

    public boolean startScan() {
        return true;
    }

    public boolean isScanAlwaysAvailable() {
        return false;
    }

    public boolean is5GHzBandSupported() {
        return true;
    }

    public boolean is6GHzBandSupported() {
        return false;
    }

    public boolean isWifiStandardSupported(int standard) {
        return true;
    }

    public WifiLock createWifiLock(int lockType, String tag) {
        return new WifiLock();
    }

    public WifiLock createWifiLock(String tag) {
        return new WifiLock();
    }

    public MulticastLock createMulticastLock(String tag) {
        return new MulticastLock();
    }
}
