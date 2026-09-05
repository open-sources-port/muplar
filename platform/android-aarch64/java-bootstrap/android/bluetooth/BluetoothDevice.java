package android.bluetooth;

import android.os.Parcel;
import android.os.Parcelable;

public final class BluetoothDevice implements Parcelable {
    public static final Parcelable.Creator<BluetoothDevice> CREATOR =
        new Parcelable.Creator<BluetoothDevice>() {
            public BluetoothDevice createFromParcel(Parcel in) {
                return new BluetoothDevice();
            }
            public BluetoothDevice[] newArray(int size) {
                return new BluetoothDevice[size];
            }
        };

    public BluetoothDevice() {
    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel dest, int flags) {
    }
}
