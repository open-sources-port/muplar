package android.util;

import java.util.ArrayList;
import java.util.TreeMap;

public class SparseBooleanArray implements Cloneable {
    private final TreeMap<Integer, Boolean> values = new TreeMap<>();
    public SparseBooleanArray() {}
    public SparseBooleanArray(int initialCapacity) {}
    public boolean get(int key) { return get(key, false); }
    public boolean get(int key, boolean fallback) {
        Boolean value = values.get(key); return value == null ? fallback : value;
    }
    public void put(int key, boolean value) { values.put(key, value); }
    public void append(int key, boolean value) { put(key, value); }
    public void delete(int key) { values.remove(key); }
    public void removeAt(int index) { values.remove(keyAt(index)); }
    public int size() { return values.size(); }
    public int keyAt(int index) {
        return new ArrayList<Integer>(values.keySet()).get(index);
    }
    public boolean valueAt(int index) {
        return new ArrayList<Boolean>(values.values()).get(index);
    }
    public int indexOfKey(int key) {
        int index = 0;
        for (Integer candidate : values.keySet()) {
            if (candidate == key) return index;
            ++index;
        }
        return -1;
    }
    public void clear() { values.clear(); }
    @Override public SparseBooleanArray clone() {
        SparseBooleanArray copy = new SparseBooleanArray(values.size());
        copy.values.putAll(values); return copy;
    }
}
