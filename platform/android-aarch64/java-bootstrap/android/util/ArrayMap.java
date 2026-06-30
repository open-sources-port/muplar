package android.util;

import java.util.LinkedHashMap;
import java.util.Map;

public class ArrayMap<K, V> extends LinkedHashMap<K, V> {
    public ArrayMap() { super(); }
    public ArrayMap(int capacity) { super(capacity); }
    public ArrayMap(Map<? extends K, ? extends V> map) { super(map); }
    public K keyAt(int index) { return entryAt(index).getKey(); }
    public V valueAt(int index) { return entryAt(index).getValue(); }
    public V setValueAt(int index, V value) {
        return put(keyAt(index), value);
    }
    public V removeAt(int index) { return remove(keyAt(index)); }
    public int indexOfKey(Object key) {
        int index = 0;
        for (K candidate : keySet()) {
            if (candidate == key || (candidate != null && candidate.equals(key)))
                return index;
            index++;
        }
        return -1;
    }
    public int indexOfValue(Object value) {
        int index = 0;
        for (V candidate : values()) {
            if (candidate == value || (candidate != null && candidate.equals(value)))
                return index;
            index++;
        }
        return -1;
    }
    private Map.Entry<K, V> entryAt(int wanted) {
        if (wanted < 0 || wanted >= size())
            throw new ArrayIndexOutOfBoundsException(wanted);
        int index = 0;
        for (Map.Entry<K, V> entry : entrySet()) {
            if (index++ == wanted) return entry;
        }
        throw new ArrayIndexOutOfBoundsException(wanted);
    }
}
