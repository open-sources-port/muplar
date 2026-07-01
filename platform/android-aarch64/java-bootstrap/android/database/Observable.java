package android.database;

import java.util.ArrayList;

public abstract class Observable<T> {
    protected final ArrayList<T> mObservers = new ArrayList<T>();

    public void registerObserver(T observer) {
        if (observer == null) throw new IllegalArgumentException("observer is null");
        synchronized (mObservers) {
            if (mObservers.contains(observer))
                throw new IllegalStateException("observer already registered");
            mObservers.add(observer);
        }
    }
    public void unregisterObserver(T observer) {
        if (observer == null) throw new IllegalArgumentException("observer is null");
        synchronized (mObservers) { mObservers.remove(observer); }
    }
    public void unregisterAll() {
        synchronized (mObservers) { mObservers.clear(); }
    }
}
