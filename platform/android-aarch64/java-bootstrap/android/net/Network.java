package android.net;

import android.os.Parcel;
import android.os.Parcelable;
import java.io.FileDescriptor;
import java.io.IOException;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.Socket;
import java.net.URLConnection;
import java.net.URL;
import javax.net.SocketFactory;

public class Network implements Parcelable {
    public final int netId;

    public Network(int netId) {
        this.netId = netId;
    }

    public int getNetId() {
        return netId;
    }

    public SocketFactory getSocketFactory() {
        return SocketFactory.getDefault();
    }

    public URLConnection openConnection(URL url) throws IOException {
        return url.openConnection();
    }

    public void bindSocket(DatagramSocket socket) throws IOException {}
    public void bindSocket(Socket socket) throws IOException {}
    public void bindSocket(FileDescriptor fd) throws IOException {}

    public InetAddress[] getAllByName(String host) throws java.net.UnknownHostException {
        return InetAddress.getAllByName(host);
    }

    public InetAddress getByName(String host) throws java.net.UnknownHostException {
        return InetAddress.getByName(host);
    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel dest, int flags) {
        dest.writeInt(netId);
    }

    public static final Creator<Network> CREATOR = new Creator<Network>() {
        @Override
        public Network createFromParcel(Parcel in) {
            return new Network(in.readInt());
        }

        @Override
        public Network[] newArray(int size) {
            return new Network[size];
        }
    };

    @Override
    public boolean equals(Object obj) {
        if (obj instanceof Network) {
            return ((Network) obj).netId == this.netId;
        }
        return false;
    }

    @Override
    public int hashCode() {
        return netId * 11;
    }

    @Override
    public String toString() {
        return Integer.toString(netId);
    }
}
