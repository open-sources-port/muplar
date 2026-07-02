package android.view;

import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.function.Consumer;

public final class CrossWindowBlurListeners {
    public static final boolean CROSS_WINDOW_BLUR_SUPPORTED = false;
    private static final CrossWindowBlurListeners INSTANCE =
        new CrossWindowBlurListeners();
    private final List<Consumer<Boolean>> listeners =
        new CopyOnWriteArrayList<Consumer<Boolean>>();
    private CrossWindowBlurListeners() {}
    public static CrossWindowBlurListeners getInstance() { return INSTANCE; }
    public boolean isCrossWindowBlurEnabled() { return false; }
    public void addListener(Consumer<Boolean> listener) {
        if (listener != null) {
            listeners.add(listener);
            listener.accept(Boolean.FALSE);
        }
    }
    public void addListener(java.util.concurrent.Executor executor,
            Consumer<Boolean> listener) {
        if (listener == null) return;
        listeners.add(listener);
        if (executor == null) listener.accept(Boolean.FALSE);
        else executor.execute(() -> listener.accept(Boolean.FALSE));
    }
    public void removeListener(Consumer<Boolean> listener) {
        listeners.remove(listener);
    }
}
