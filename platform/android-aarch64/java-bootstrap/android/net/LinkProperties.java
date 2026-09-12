package android.net;

import android.os.Parcel;
import android.os.Parcelable;
import java.net.InetAddress;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

public final class LinkProperties implements Parcelable {
    private String mIfaceName = "wlan0";
    private final ArrayList<InetAddress> mDnses = new ArrayList<>();
    private int mMtu = 1500;

    public LinkProperties() {}

    public LinkProperties(LinkProperties source) {
        if (source != null) {
            this.mIfaceName = source.mIfaceName;
            this.mDnses.addAll(source.mDnses);
            this.mMtu = source.mMtu;
        }
    }

    public void setInterfaceName(String iface) {
        this.mIfaceName = iface;
    }

    public String getInterfaceName() {
        return mIfaceName;
    }

    public List<InetAddress> getDnsServers() {
        return Collections.unmodifiableList(mDnses);
    }

    public boolean addDnsServer(InetAddress dns) {
        if (dns != null && !mDnses.contains(dns)) {
            mDnses.add(dns);
            return true;
        }
        return false;
    }

    public void setMtu(int mtu) {
        this.mMtu = mtu;
    }

    public int getMtu() {
        return mMtu;
    }

    public void clear() {
        mIfaceName = null;
        mDnses.clear();
        mMtu = 0;
    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel dest, int flags) {
        dest.writeString(mIfaceName);
        dest.writeInt(mMtu);
    }

    public static final Creator<LinkProperties> CREATOR = new Creator<LinkProperties>() {
        @Override
        public LinkProperties createFromParcel(Parcel in) {
            LinkProperties lp = new LinkProperties();
            lp.mIfaceName = in.readString();
            lp.mMtu = in.readInt();
            return lp;
        }

        @Override
        public LinkProperties[] newArray(int size) {
            return new LinkProperties[size];
        }
    };
}
