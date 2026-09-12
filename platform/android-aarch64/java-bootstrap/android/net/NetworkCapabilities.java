package android.net;

import android.os.Parcel;
import android.os.Parcelable;

public final class NetworkCapabilities implements Parcelable {
    public static final int NET_CAPABILITY_MMS = 0;
    public static final int NET_CAPABILITY_SUPL = 1;
    public static final int NET_CAPABILITY_DUN = 2;
    public static final int NET_CAPABILITY_FOTA = 3;
    public static final int NET_CAPABILITY_IMS = 4;
    public static final int NET_CAPABILITY_CBS = 5;
    public static final int NET_CAPABILITY_WIFI_P2P = 6;
    public static final int NET_CAPABILITY_IA = 7;
    public static final int NET_CAPABILITY_RCS = 8;
    public static final int NET_CAPABILITY_XCAP = 9;
    public static final int NET_CAPABILITY_EIMS = 10;
    public static final int NET_CAPABILITY_NOT_METERED = 11;
    public static final int NET_CAPABILITY_INTERNET = 12;
    public static final int NET_CAPABILITY_NOT_RESTRICTED = 13;
    public static final int NET_CAPABILITY_TRUSTED = 14;
    public static final int NET_CAPABILITY_NOT_VPN = 15;
    public static final int NET_CAPABILITY_VALIDATED = 16;
    public static final int NET_CAPABILITY_CAPTIVE_PORTAL = 17;
    public static final int NET_CAPABILITY_NOT_ROAMING = 18;
    public static final int NET_CAPABILITY_FOREGROUND = 19;
    public static final int NET_CAPABILITY_NOT_CONGESTED = 20;
    public static final int NET_CAPABILITY_NOT_SUSPENDED = 21;

    public static final int TRANSPORT_CELLULAR = 0;
    public static final int TRANSPORT_WIFI = 1;
    public static final int TRANSPORT_BLUETOOTH = 2;
    public static final int TRANSPORT_ETHERNET = 3;
    public static final int TRANSPORT_VPN = 4;
    public static final int TRANSPORT_WIFI_AWARE = 5;
    public static final int TRANSPORT_LOWPAN = 6;
    public static final int TRANSPORT_TEST = 7;
    public static final int TRANSPORT_USB = 8;

    public static final int SIGNAL_STRENGTH_UNSPECIFIED = Integer.MIN_VALUE;

    private long mNetworkCapabilities = 0;
    private long mTransportTypes = 0;
    private int mLinkUpBandwidthKbps = 102400;
    private int mLinkDownBandwidthKbps = 102400;
    private int mSignalStrength = -50;

    public NetworkCapabilities() {
        addCapability(NET_CAPABILITY_INTERNET);
        addCapability(NET_CAPABILITY_NOT_RESTRICTED);
        addCapability(NET_CAPABILITY_TRUSTED);
        addCapability(NET_CAPABILITY_NOT_VPN);
        addCapability(NET_CAPABILITY_VALIDATED);
        addTransportType(TRANSPORT_WIFI);
    }

    public NetworkCapabilities(NetworkCapabilities nc) {
        if (nc != null) {
            this.mNetworkCapabilities = nc.mNetworkCapabilities;
            this.mTransportTypes = nc.mTransportTypes;
            this.mLinkUpBandwidthKbps = nc.mLinkUpBandwidthKbps;
            this.mLinkDownBandwidthKbps = nc.mLinkDownBandwidthKbps;
            this.mSignalStrength = nc.mSignalStrength;
        }
    }

    public NetworkCapabilities addCapability(int capability) {
        if (capability >= 0 && capability < 64) {
            mNetworkCapabilities |= (1L << capability);
        }
        return this;
    }

    public NetworkCapabilities removeCapability(int capability) {
        if (capability >= 0 && capability < 64) {
            mNetworkCapabilities &= ~(1L << capability);
        }
        return this;
    }

    public boolean hasCapability(int capability) {
        if (capability >= 0 && capability < 64) {
            return (mNetworkCapabilities & (1L << capability)) != 0;
        }
        return false;
    }

    public int[] getCapabilities() {
        int count = Long.bitCount(mNetworkCapabilities);
        int[] result = new int[count];
        int idx = 0;
        for (int i = 0; i < 64; i++) {
            if ((mNetworkCapabilities & (1L << i)) != 0) {
                result[idx++] = i;
            }
        }
        return result;
    }

    public NetworkCapabilities addTransportType(int transportType) {
        if (transportType >= 0 && transportType < 64) {
            mTransportTypes |= (1L << transportType);
        }
        return this;
    }

    public NetworkCapabilities removeTransportType(int transportType) {
        if (transportType >= 0 && transportType < 64) {
            mTransportTypes &= ~(1L << transportType);
        }
        return this;
    }

    public boolean hasTransport(int transportType) {
        if (transportType >= 0 && transportType < 64) {
            return (mTransportTypes & (1L << transportType)) != 0;
        }
        return false;
    }

    public int[] getTransportTypes() {
        int count = Long.bitCount(mTransportTypes);
        int[] result = new int[count];
        int idx = 0;
        for (int i = 0; i < 64; i++) {
            if ((mTransportTypes & (1L << i)) != 0) {
                result[idx++] = i;
            }
        }
        return result;
    }

    public int getLinkUpstreamBandwidthKbps() {
        return mLinkUpBandwidthKbps;
    }

    public void setLinkUpstreamBandwidthKbps(int upKbps) {
        mLinkUpBandwidthKbps = upKbps;
    }

    public int getLinkDownstreamBandwidthKbps() {
        return mLinkDownBandwidthKbps;
    }

    public void setLinkDownstreamBandwidthKbps(int downKbps) {
        mLinkDownBandwidthKbps = downKbps;
    }

    public int getSignalStrength() {
        return mSignalStrength;
    }

    public void setSignalStrength(int signalStrength) {
        mSignalStrength = signalStrength;
    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel dest, int flags) {
        dest.writeLong(mNetworkCapabilities);
        dest.writeLong(mTransportTypes);
        dest.writeInt(mLinkUpBandwidthKbps);
        dest.writeInt(mLinkDownBandwidthKbps);
        dest.writeInt(mSignalStrength);
    }

    public static final Creator<NetworkCapabilities> CREATOR = new Creator<NetworkCapabilities>() {
        @Override
        public NetworkCapabilities createFromParcel(Parcel in) {
            NetworkCapabilities nc = new NetworkCapabilities();
            nc.mNetworkCapabilities = in.readLong();
            nc.mTransportTypes = in.readLong();
            nc.mLinkUpBandwidthKbps = in.readInt();
            nc.mLinkDownBandwidthKbps = in.readInt();
            nc.mSignalStrength = in.readInt();
            return nc;
        }

        @Override
        public NetworkCapabilities[] newArray(int size) {
            return new NetworkCapabilities[size];
        }
    };
}
