package android.os;

import java.util.HashMap;
import java.util.Map;
import android.util.SparseArray;

public class Bundle implements Parcelable, Cloneable {
    public static final Bundle EMPTY = new Bundle();
    private final Map<String, Object> values = new HashMap<>();
    public Bundle() {}
    public Bundle(Bundle source) {
        if (source != null) values.putAll(source.values);
    }
    public void putInt(String key, int value) { values.put(key, value); }
    public int getInt(String key) { return getInt(key, 0); }
    public int getInt(String key, int fallback) {
        Object value = values.get(key);
        return value instanceof Integer ? (Integer)value : fallback;
    }
    public void putLong(String key, long value) { values.put(key, value); }
    public long getLong(String key, long fallback) {
        Object value = values.get(key);
        return value instanceof Long ? (Long)value : fallback;
    }
    public void putBoolean(String key, boolean value) { values.put(key, value); }
    public boolean getBoolean(String key, boolean fallback) {
        Object value = values.get(key);
        return value instanceof Boolean ? (Boolean)value : fallback;
    }
    public void putString(String key, String value) { values.put(key, value); }
    public String getString(String key) {
        Object value = values.get(key);
        return value instanceof String ? (String)value : null;
    }
    public void putParcelable(String key, Parcelable value) { values.put(key, value); }
    public <T extends Parcelable> T getParcelable(String key) {
        return (T)values.get(key);
    }
    public void putSparseParcelableArray(String key,
            SparseArray<? extends Parcelable> value) { values.put(key, value); }
    public <T extends Parcelable> SparseArray<T> getSparseParcelableArray(String key) {
        Object value = values.get(key);
        return value instanceof SparseArray ? (SparseArray<T>) value : null;
    }
    public boolean containsKey(String key) { return values.containsKey(key); }
    public Object get(String key) { return values.get(key); }
    public void remove(String key) { values.remove(key); }
    public int size() { return values.size(); }
    public boolean isEmpty() { return values.isEmpty(); }
    public void clear() { values.clear(); }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel destination, int flags) {}
    @Override public Object clone() { return new Bundle(this); }
}
