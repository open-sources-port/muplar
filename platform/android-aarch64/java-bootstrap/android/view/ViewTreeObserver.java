package android.view;

import java.util.ArrayList;
import java.util.List;

public final class ViewTreeObserver {
    public interface OnGlobalLayoutListener { void onGlobalLayout(); }
    public interface OnPreDrawListener { boolean onPreDraw(); }
    public interface OnDrawListener { void onDraw(); }
    public interface OnScrollChangedListener { void onScrollChanged(); }
    public interface OnTouchModeChangeListener {
        void onTouchModeChanged(boolean isInTouchMode);
    }
    public interface OnWindowAttachListener {
        void onWindowAttached();
        void onWindowDetached();
    }
    public interface OnWindowFocusChangeListener {
        void onWindowFocusChanged(boolean hasFocus);
    }
    public interface OnGlobalFocusChangeListener {
        void onGlobalFocusChanged(View oldFocus, View newFocus);
    }

    private final List<OnGlobalLayoutListener> globalLayoutListeners =
        new ArrayList<>();
    private final List<OnPreDrawListener> preDrawListeners = new ArrayList<>();
    private final List<OnDrawListener> drawListeners = new ArrayList<>();

    public boolean isAlive() { return true; }
    public void addOnGlobalLayoutListener(OnGlobalLayoutListener listener) {
        if (listener != null && !globalLayoutListeners.contains(listener))
            globalLayoutListeners.add(listener);
    }
    public void removeOnGlobalLayoutListener(OnGlobalLayoutListener listener) {
        globalLayoutListeners.remove(listener);
    }
    public void removeGlobalOnLayoutListener(OnGlobalLayoutListener listener) {
        removeOnGlobalLayoutListener(listener);
    }
    public void addOnPreDrawListener(OnPreDrawListener listener) {
        if (listener != null && !preDrawListeners.contains(listener))
            preDrawListeners.add(listener);
    }
    public void removeOnPreDrawListener(OnPreDrawListener listener) {
        preDrawListeners.remove(listener);
    }
    public void addOnDrawListener(OnDrawListener listener) {
        if (listener != null && !drawListeners.contains(listener))
            drawListeners.add(listener);
    }
    public void removeOnDrawListener(OnDrawListener listener) {
        drawListeners.remove(listener);
    }
    public void dispatchOnGlobalLayout() {
        for (OnGlobalLayoutListener listener :
                new ArrayList<>(globalLayoutListeners))
            listener.onGlobalLayout();
    }
    public boolean dispatchOnPreDraw() {
        boolean proceed = true;
        for (OnPreDrawListener listener : new ArrayList<>(preDrawListeners))
            proceed &= listener.onPreDraw();
        return proceed;
    }
    public void dispatchOnDraw() {
        for (OnDrawListener listener : new ArrayList<>(drawListeners))
            listener.onDraw();
    }
}
