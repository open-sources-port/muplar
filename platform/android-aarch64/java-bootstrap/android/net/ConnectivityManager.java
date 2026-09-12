package android.net;

import android.content.Context;
import android.os.Handler;

public class ConnectivityManager {
    public static final String CONNECTIVITY_ACTION = "android.net.conn.CONNECTIVITY_CHANGE";
    public static final String EXTRA_NETWORK_INFO = "networkInfo";
    public static final String EXTRA_IS_FAILOVER = "isFailover";
    public static final String EXTRA_OTHER_NETWORK_INFO = "otherNetwork";
    public static final String EXTRA_NO_CONNECTIVITY = "noConnectivity";
    public static final String EXTRA_REASON = "reason";
    public static final String EXTRA_EXTRA_INFO = "extraInfo";

    public static final int TYPE_NONE = -1;
    public static final int TYPE_MOBILE = 0;
    public static final int TYPE_WIFI = 1;
    public static final int TYPE_MOBILE_MMS = 2;
    public static final int TYPE_MOBILE_SUPL = 3;
    public static final int TYPE_MOBILE_DUN = 4;
    public static final int TYPE_MOBILE_HIPRI = 5;
    public static final int TYPE_WIMAX = 6;
    public static final int TYPE_BLUETOOTH = 7;
    public static final int TYPE_DUMMY = 8;
    public static final int TYPE_ETHERNET = 9;
    public static final int TYPE_VPN = 17;

    public static final int RESTRICT_BACKGROUND_STATUS_DISABLED = 1;
    public static final int RESTRICT_BACKGROUND_STATUS_WHITELISTED = 2;
    public static final int RESTRICT_BACKGROUND_STATUS_ENABLED = 3;

    private final Context mContext;
    private final Network mNetwork;
    private final NetworkCapabilities mCapabilities;
    private final NetworkInfo mNetworkInfo;
    private final LinkProperties mLinkProperties;

    public ConnectivityManager() {
        this(null);
    }

    public ConnectivityManager(Context context) {
        this.mContext = context;
        this.mNetwork = createMockNetwork();
        this.mCapabilities = createMockCapabilities();
        this.mNetworkInfo = createMockNetworkInfo();
        this.mLinkProperties = createMockLinkProperties();
        System.out.println("[Muplar/ART] ConnectivityManager instance created context=" + (context != null));
    }

    public ConnectivityManager(Context context, Object service) {
        this(context);
    }

    private static Network createMockNetwork() {
        try {
            java.lang.reflect.Constructor<?> ctor = Network.class.getDeclaredConstructor(Integer.TYPE);
            ctor.setAccessible(true);
            return (Network) ctor.newInstance(100);
        } catch (Throwable t) {
            try {
                for (java.lang.reflect.Constructor<?> ctor : Network.class.getDeclaredConstructors()) {
                    ctor.setAccessible(true);
                    if (ctor.getParameterTypes().length == 1 && ctor.getParameterTypes()[0] == Integer.TYPE) {
                        return (Network) ctor.newInstance(100);
                    }
                }
            } catch (Throwable ignored) {}
        }
        return null;
    }

    private static NetworkCapabilities createMockCapabilities() {
        try {
            java.lang.reflect.Constructor<?> ctor = NetworkCapabilities.class.getDeclaredConstructor();
            ctor.setAccessible(true);
            NetworkCapabilities nc = (NetworkCapabilities) ctor.newInstance();
            try {
                java.lang.reflect.Method addCap = NetworkCapabilities.class.getMethod("addCapability", Integer.TYPE);
                addCap.invoke(nc, 12); // NET_CAPABILITY_INTERNET
                addCap.invoke(nc, 13); // NET_CAPABILITY_NOT_RESTRICTED
                addCap.invoke(nc, 14); // NET_CAPABILITY_TRUSTED
                addCap.invoke(nc, 15); // NET_CAPABILITY_NOT_VPN
                addCap.invoke(nc, 16); // NET_CAPABILITY_VALIDATED
            } catch (Throwable ignored) {}
            try {
                java.lang.reflect.Method addTransport = NetworkCapabilities.class.getMethod("addTransportType", Integer.TYPE);
                addTransport.invoke(nc, 1); // TRANSPORT_WIFI
            } catch (Throwable ignored) {}
            return nc;
        } catch (Throwable ignored) {}
        return null;
    }

    private static NetworkInfo createMockNetworkInfo() {
        try {
            for (java.lang.reflect.Constructor<?> ctor : NetworkInfo.class.getDeclaredConstructors()) {
                ctor.setAccessible(true);
                Class<?>[] p = ctor.getParameterTypes();
                if (p.length == 4 && p[0] == Integer.TYPE && p[1] == Integer.TYPE && p[2] == String.class && p[3] == String.class) {
                    NetworkInfo ni = (NetworkInfo) ctor.newInstance(TYPE_WIFI, 0, "WIFI", "");
                    try {
                        Class<?> detailedStateClass = Class.forName("android.net.NetworkInfo$DetailedState");
                        @SuppressWarnings({"unchecked", "rawtypes"})
                        Object connectedState = Enum.valueOf((Class<Enum>) detailedStateClass, "CONNECTED");
                        java.lang.reflect.Method setDetailed = NetworkInfo.class.getMethod("setDetailedState", detailedStateClass, String.class, String.class);
                        setDetailed.invoke(ni, connectedState, null, null);
                    } catch (Throwable ignored) {}
                    try {
                        Class<?> stateClass = Class.forName("android.net.NetworkInfo$State");
                        @SuppressWarnings({"unchecked", "rawtypes"})
                        Object connectedState = Enum.valueOf((Class<Enum>) stateClass, "CONNECTED");
                        java.lang.reflect.Field stateField = NetworkInfo.class.getDeclaredField("mState");
                        stateField.setAccessible(true);
                        stateField.set(ni, connectedState);
                    } catch (Throwable ignored) {}
                    return ni;
                }
            }
        } catch (Throwable ignored) {}
        return null;
    }

    private static LinkProperties createMockLinkProperties() {
        try {
            java.lang.reflect.Constructor<?> ctor = LinkProperties.class.getDeclaredConstructor();
            ctor.setAccessible(true);
            LinkProperties lp = (LinkProperties) ctor.newInstance();
            try {
                java.lang.reflect.Method setIface = LinkProperties.class.getMethod("setInterfaceName", String.class);
                setIface.invoke(lp, "wlan0");
            } catch (Throwable ignored) {}
            return lp;
        } catch (Throwable ignored) {}
        return null;
    }

    public Network getActiveNetwork() {
        return mNetwork;
    }

    public NetworkCapabilities getNetworkCapabilities(Network network) {
        return mCapabilities;
    }

    public NetworkInfo getActiveNetworkInfo() {
        return mNetworkInfo;
    }

    public NetworkInfo getNetworkInfo(int networkType) {
        return mNetworkInfo;
    }

    public NetworkInfo getNetworkInfo(Network network) {
        return mNetworkInfo;
    }

    public Network[] getAllNetworks() {
        return mNetwork != null ? new Network[] { mNetwork } : new Network[0];
    }

    public NetworkInfo[] getAllNetworkInfo() {
        return mNetworkInfo != null ? new NetworkInfo[] { mNetworkInfo } : new NetworkInfo[0];
    }

    public LinkProperties getLinkProperties(Network network) {
        return mLinkProperties;
    }

    public boolean isActiveNetworkMetered() {
        return false;
    }

    public boolean isDefaultNetworkActive() {
        return true;
    }

    public int getRestrictBackgroundStatus() {
        return RESTRICT_BACKGROUND_STATUS_DISABLED;
    }

    public void registerDefaultNetworkCallback(NetworkCallback networkCallback) {
        registerDefaultNetworkCallback(networkCallback, null);
    }

    public void registerDefaultNetworkCallback(NetworkCallback networkCallback, Handler handler) {
        if (networkCallback != null) {
            try {
                if (mNetwork != null) {
                    networkCallback.onAvailable(mNetwork);
                    if (mCapabilities != null) {
                        networkCallback.onCapabilitiesChanged(mNetwork, mCapabilities);
                    }
                    if (mLinkProperties != null) {
                        networkCallback.onLinkPropertiesChanged(mNetwork, mLinkProperties);
                    }
                }
            } catch (Throwable ignored) {}
        }
    }

    public void registerNetworkCallback(NetworkRequest request, NetworkCallback networkCallback) {
        registerDefaultNetworkCallback(networkCallback);
    }

    public void registerNetworkCallback(NetworkRequest request, NetworkCallback networkCallback, Handler handler) {
        registerDefaultNetworkCallback(networkCallback, handler);
    }

    public void unregisterNetworkCallback(NetworkCallback networkCallback) {
    }

    public static class NetworkCallback {
        public void onAvailable(Network network) {}
        public void onLosing(Network network, int maxMsToLive) {}
        public void onLost(Network network) {}
        public void onUnavailable() {}
        public void onCapabilitiesChanged(Network network, NetworkCapabilities networkCapabilities) {}
        public void onLinkPropertiesChanged(Network network, LinkProperties linkProperties) {}
        public void onBlockedStatusChanged(Network network, boolean blocked) {}
    }

    public interface OnNetworkActiveListener {
        void onNetworkActive();
    }

    public void addDefaultNetworkActiveListener(OnNetworkActiveListener listener) {}
    public void removeDefaultNetworkActiveListener(OnNetworkActiveListener listener) {}
}
