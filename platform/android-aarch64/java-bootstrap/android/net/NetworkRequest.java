package android.net;

import android.os.Parcel;
import android.os.Parcelable;

public class NetworkRequest implements Parcelable {
    public final NetworkCapabilities networkCapabilities;
    public final int requestId;

    public NetworkRequest() {
        this(new NetworkCapabilities(), 0);
    }

    public NetworkRequest(NetworkCapabilities nc, int rId) {
        this.networkCapabilities = nc != null ? nc : new NetworkCapabilities();
        this.requestId = rId;
    }

    public boolean hasCapability(int capability) {
        return networkCapabilities.hasCapability(capability);
    }

    public boolean hasTransport(int transportType) {
        return networkCapabilities.hasTransport(transportType);
    }

    public int[] getCapabilities() {
        return networkCapabilities.getCapabilities();
    }

    public int[] getTransportTypes() {
        return networkCapabilities.getTransportTypes();
    }

    public NetworkCapabilities getNetworkCapabilities() {
        return networkCapabilities;
    }

    public static class Builder {
        private final NetworkCapabilities mCapabilities = new NetworkCapabilities();

        public Builder addCapability(int capability) {
            mCapabilities.addCapability(capability);
            return this;
        }

        public Builder removeCapability(int capability) {
            mCapabilities.removeCapability(capability);
            return this;
        }

        public Builder addTransportType(int transportType) {
            mCapabilities.addTransportType(transportType);
            return this;
        }

        public Builder removeTransportType(int transportType) {
            mCapabilities.removeTransportType(transportType);
            return this;
        }

        public Builder clearCapabilities() {
            for (int cap : mCapabilities.getCapabilities()) {
                mCapabilities.removeCapability(cap);
            }
            return this;
        }

        public NetworkRequest build() {
            return new NetworkRequest(new NetworkCapabilities(mCapabilities), 1);
        }
    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel dest, int flags) {
        networkCapabilities.writeToParcel(dest, flags);
        dest.writeInt(requestId);
    }

    public static final Creator<NetworkRequest> CREATOR = new Creator<NetworkRequest>() {
        @Override
        public NetworkRequest createFromParcel(Parcel in) {
            NetworkCapabilities nc = NetworkCapabilities.CREATOR.createFromParcel(in);
            int rId = in.readInt();
            return new NetworkRequest(nc, rId);
        }

        @Override
        public NetworkRequest[] newArray(int size) {
            return new NetworkRequest[size];
        }
    };
}
