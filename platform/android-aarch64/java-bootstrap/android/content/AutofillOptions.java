package android.content;

import android.os.Parcel;
import android.os.Parcelable;

public final class AutofillOptions implements Parcelable {
    @Override
    public int describeContents() {
        return 0;
    }
    @Override
    public void writeToParcel(Parcel dest, int flags) {
    }
    public static final Parcelable.Creator<AutofillOptions> CREATOR =
        new Parcelable.Creator<AutofillOptions>() {
            @Override
            public AutofillOptions createFromParcel(Parcel source) {
                return null;
            }
            @Override
            public AutofillOptions[] newArray(int size) {
                return new AutofillOptions[size];
            }
        };
}
