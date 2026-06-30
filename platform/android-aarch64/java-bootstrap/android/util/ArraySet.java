package android.util;

import java.util.Collection;
import java.util.LinkedHashSet;

public class ArraySet<E> extends LinkedHashSet<E> {
    public ArraySet() { super(); }
    public ArraySet(int capacity) { super(capacity); }
    public ArraySet(Collection<? extends E> values) { super(values); }
    public E valueAt(int wanted) {
        if (wanted < 0 || wanted >= size())
            throw new ArrayIndexOutOfBoundsException(wanted);
        int index = 0;
        for (E value : this) if (index++ == wanted) return value;
        throw new ArrayIndexOutOfBoundsException(wanted);
    }
    public E removeAt(int index) {
        E value = valueAt(index); remove(value); return value;
    }
}
