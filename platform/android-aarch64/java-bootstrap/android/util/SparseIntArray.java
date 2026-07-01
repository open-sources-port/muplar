package android.util;

import java.util.ArrayList;
import java.util.TreeMap;

public class SparseIntArray implements Cloneable {
    private final TreeMap<Integer, Integer> values = new TreeMap<Integer, Integer>();
    public SparseIntArray() {}
    public SparseIntArray(int initialCapacity) {}
    public int get(int key) { return get(key, 0); }
    public int get(int key, int fallback) {
        Integer value = values.get(key);
        return value == null ? fallback : value;
    }
    public void put(int key, int value) { values.put(key, value); }
    public void append(int key, int value) { put(key, value); }
    public void delete(int key) { values.remove(key); }
    public void removeAt(int index) { values.remove(keyAt(index)); }
    public int size() { return values.size(); }
    public int keyAt(int index) {
        return new ArrayList<Integer>(values.keySet()).get(index);
    }
    public int valueAt(int index) {
        return new ArrayList<Integer>(values.values()).get(index);
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
    @Override public SparseIntArray clone() {
        SparseIntArray copy = new SparseIntArray(values.size());
        copy.values.putAll(values);
        return copy;
    }
}
