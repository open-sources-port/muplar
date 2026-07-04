package android.content;

import android.os.Parcel;
import android.os.Parcelable;

public final class LocusId implements Parcelable {
    private final String id;
    public LocusId(String id) { this.id = id == null ? "" : id; }
    public String getId() { return id; }
    @Override public boolean equals(Object other) {
        return other instanceof LocusId && id.equals(((LocusId) other).id);
    }
    @Override public int hashCode() { return id.hashCode(); }
    @Override public String toString() { return "LocusId[" + id + "]"; }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel destination, int flags) {
        destination.writeString(id);
    }
}
