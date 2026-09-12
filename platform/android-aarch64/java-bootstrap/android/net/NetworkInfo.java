package android.net;

import android.os.Parcel;
import android.os.Parcelable;

public class NetworkInfo implements Parcelable {
    public enum State {
        CONNECTING, CONNECTED, SUSPENDED, DISCONNECTING, DISCONNECTED, UNKNOWN
    }

    public enum DetailedState {
        IDLE, SCANNING, CONNECTING, AUTHENTICATING, OBTAINING_IPADDR,
        CONNECTED, SUSPENDED, DISCONNECTING, DISCONNECTED, FAILED, BLOCKED,
        VERIFYING_POOR_LINK, CAPTIVE_PORTAL_CHECK
    }

    private int mNetworkType;
    private int mSubtype;
    private String mTypeName;
    private String mSubtypeName;
    private State mState;
    private DetailedState mDetailedState;
    private String mReason;
    private String mExtraInfo;
    private boolean mIsFailover;
    private boolean mIsRoaming;
    private boolean mIsAvailable;

    public NetworkInfo(int type, int subtype, String typeName, String subtypeName) {
        this.mNetworkType = type;
        this.mSubtype = subtype;
        this.mTypeName = typeName;
        this.mSubtypeName = subtypeName;
        this.mState = State.CONNECTED;
        this.mDetailedState = DetailedState.CONNECTED;
        this.mIsAvailable = true;
        this.mIsRoaming = false;
        this.mIsFailover = false;
        this.mReason = "";
        this.mExtraInfo = "";
    }

    public int getType() {
        return mNetworkType;
    }

    public int getSubtype() {
        return mSubtype;
    }

    public String getTypeName() {
        return mTypeName;
    }

    public String getSubtypeName() {
        return mSubtypeName;
    }

    public boolean isConnectedOrConnecting() {
        return mState == State.CONNECTED || mState == State.CONNECTING;
    }

    public boolean isConnected() {
        return mState == State.CONNECTED;
    }

    public boolean isAvailable() {
        return mIsAvailable;
    }

    public boolean isFailover() {
        return mIsFailover;
    }

    public boolean isRoaming() {
        return mIsRoaming;
    }

    public State getState() {
        return mState;
    }

    public DetailedState getDetailedState() {
        return mDetailedState;
    }

    public void setDetailedState(DetailedState detailedState, String reason, String extraInfo) {
        this.mDetailedState = detailedState;
        this.mReason = reason;
        this.mExtraInfo = extraInfo;
        if (detailedState == DetailedState.CONNECTED) {
            this.mState = State.CONNECTED;
        } else if (detailedState == DetailedState.DISCONNECTED || detailedState == DetailedState.FAILED) {
            this.mState = State.DISCONNECTED;
        } else if (detailedState == DetailedState.CONNECTING || detailedState == DetailedState.OBTAINING_IPADDR) {
            this.mState = State.CONNECTING;
        }
    }

    public String getReason() {
        return mReason;
    }

    public String getExtraInfo() {
        return mExtraInfo;
    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel dest, int flags) {
        dest.writeInt(mNetworkType);
        dest.writeInt(mSubtype);
        dest.writeString(mTypeName);
        dest.writeString(mSubtypeName);
        dest.writeString(mState.name());
        dest.writeString(mDetailedState.name());
        dest.writeInt(mIsFailover ? 1 : 0);
        dest.writeInt(mIsAvailable ? 1 : 0);
        dest.writeInt(mIsRoaming ? 1 : 0);
        dest.writeString(mReason);
        dest.writeString(mExtraInfo);
    }

    public static final Creator<NetworkInfo> CREATOR = new Creator<NetworkInfo>() {
        @Override
        public NetworkInfo createFromParcel(Parcel in) {
            int type = in.readInt();
            int subtype = in.readInt();
            String typeName = in.readString();
            String subtypeName = in.readString();
            NetworkInfo info = new NetworkInfo(type, subtype, typeName, subtypeName);
            info.mState = State.valueOf(in.readString());
            info.mDetailedState = DetailedState.valueOf(in.readString());
            info.mIsFailover = in.readInt() != 0;
            info.mIsAvailable = in.readInt() != 0;
            info.mIsRoaming = in.readInt() != 0;
            info.mReason = in.readString();
            info.mExtraInfo = in.readString();
            return info;
        }

        @Override
        public NetworkInfo[] newArray(int size) {
            return new NetworkInfo[size];
        }
    };
}
