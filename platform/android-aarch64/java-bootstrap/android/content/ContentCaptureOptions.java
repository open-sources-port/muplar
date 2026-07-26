package android.content;

import android.os.Parcel;
import android.os.Parcelable;

public final class ContentCaptureOptions implements Parcelable {
    @Override
    public int describeContents() {
        return 0;
    }
    @Override
    public void writeToParcel(Parcel dest, int flags) {
    }
    public static final Parcelable.Creator<ContentCaptureOptions> CREATOR =
        new Parcelable.Creator<ContentCaptureOptions>() {
            @Override
            public ContentCaptureOptions createFromParcel(Parcel source) {
                return null;
            }
            @Override
            public ContentCaptureOptions[] newArray(int size) {
                return new ContentCaptureOptions[size];
            }
        };
}
