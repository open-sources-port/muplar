package android.util;

import java.util.ArrayList;
import java.util.Map;
import java.util.TreeMap;

public class SparseArray<E> implements Cloneable {
    private final TreeMap<Integer, E> values;

    public SparseArray() { this(10); }
    public SparseArray(int initialCapacity) { values = new TreeMap<>(); }

    public E get(int key) { return values.get(key); }
    public E get(int key, E valueIfKeyNotFound) {
        E value = values.get(key);
        return value != null || values.containsKey(key) ? value : valueIfKeyNotFound;
    }
    public E getOrDefault(int key, E defaultValue) {
        return get(key, defaultValue);
    }
    public void delete(int key) { values.remove(key); }
    public void remove(int key) { values.remove(key); }
    public void removeAt(int index) { values.remove(keyAt(index)); }
    public void removeAtRange(int index, int size) {
        for (int i = Math.min(index + size, size()) - 1; i >= index; i--)
            removeAt(i);
    }
    public void put(int key, E value) { values.put(key, value); }
    public void append(int key, E value) { put(key, value); }
    public E putIfAbsent(int key, E value) {
        return values.putIfAbsent(key, value);
    }
    public int size() { return values.size(); }
    public int keyAt(int index) { return entryAt(index).getKey(); }
    public E valueAt(int index) { return entryAt(index).getValue(); }
    public void setValueAt(int index, E value) { values.put(keyAt(index), value); }
    public int indexOfKey(int key) {
        int index = 0;
        for (int candidate : values.keySet()) {
            if (candidate == key) return index;
            index++;
        }
        return -1;
    }
    public int indexOfValue(E value) {
        int index = 0;
        for (E candidate : values.values()) {
            if (candidate == value) return index;
            index++;
        }
        return -1;
    }
    public boolean contains(int key) { return values.containsKey(key); }
    public void clear() { values.clear(); }

    @Override
    public SparseArray<E> clone() {
        SparseArray<E> copy = new SparseArray<>(size());
        copy.values.putAll(values);
        return copy;
    }

    @Override
    public String toString() { return values.toString(); }

    private Map.Entry<Integer, E> entryAt(int index) {
        if (index < 0 || index >= size())
            throw new ArrayIndexOutOfBoundsException(index);
        return new ArrayList<>(values.entrySet()).get(index);
    }
}
