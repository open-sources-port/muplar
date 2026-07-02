package android.util;

import java.util.ArrayList;
import java.util.Map;
import java.util.TreeMap;

public class LongSparseArray<E> implements Cloneable {
    private final TreeMap<Long, E> values = new TreeMap<Long, E>();
    public LongSparseArray() {}
    public LongSparseArray(int initialCapacity) {}
    public E get(long key) { return values.get(key); }
    public E get(long key, E fallback) {
        E value = values.get(key);
        return value != null || values.containsKey(key) ? value : fallback;
    }
    public void put(long key, E value) { values.put(key, value); }
    public void append(long key, E value) { put(key, value); }
    public void delete(long key) { values.remove(key); }
    public void remove(long key) { values.remove(key); }
    public void removeAt(int index) { values.remove(keyAt(index)); }
    public int size() { return values.size(); }
    public long keyAt(int index) { return entryAt(index).getKey(); }
    public E valueAt(int index) { return entryAt(index).getValue(); }
    public void setValueAt(int index, E value) { values.put(keyAt(index), value); }
    public int indexOfKey(long key) {
        int index = 0;
        for (Long candidate : values.keySet()) {
            if (candidate == key) return index;
            ++index;
        }
        return -1;
    }
    public void clear() { values.clear(); }
    @Override public LongSparseArray<E> clone() {
        LongSparseArray<E> copy = new LongSparseArray<E>(size());
        copy.values.putAll(values);
        return copy;
    }
    private Map.Entry<Long, E> entryAt(int index) {
        if (index < 0 || index >= values.size())
            throw new ArrayIndexOutOfBoundsException(index);
        return new ArrayList<Map.Entry<Long, E>>(values.entrySet()).get(index);
    }
}
