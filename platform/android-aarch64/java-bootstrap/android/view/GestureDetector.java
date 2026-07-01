package android.view;

import android.content.Context;
import android.os.Handler;

public class GestureDetector {
    public interface OnGestureListener {
        boolean onDown(MotionEvent event);
        void onShowPress(MotionEvent event);
        boolean onSingleTapUp(MotionEvent event);
        boolean onScroll(MotionEvent first, MotionEvent current,
                float distanceX, float distanceY);
        void onLongPress(MotionEvent event);
        boolean onFling(MotionEvent first, MotionEvent current,
                float velocityX, float velocityY);
    }
    public interface OnDoubleTapListener {
        boolean onSingleTapConfirmed(MotionEvent event);
        boolean onDoubleTap(MotionEvent event);
        boolean onDoubleTapEvent(MotionEvent event);
    }
    public static class SimpleOnGestureListener
            implements OnGestureListener, OnDoubleTapListener {
        public boolean onDown(MotionEvent event) { return false; }
        public void onShowPress(MotionEvent event) {}
        public boolean onSingleTapUp(MotionEvent event) { return false; }
        public boolean onScroll(MotionEvent first, MotionEvent current,
                float distanceX, float distanceY) { return false; }
        public void onLongPress(MotionEvent event) {}
        public boolean onFling(MotionEvent first, MotionEvent current,
                float velocityX, float velocityY) { return false; }
        public boolean onSingleTapConfirmed(MotionEvent event) { return false; }
        public boolean onDoubleTap(MotionEvent event) { return false; }
        public boolean onDoubleTapEvent(MotionEvent event) { return false; }
    }
    private final OnGestureListener listener;
    public GestureDetector(Context context, OnGestureListener listener) {
        this.listener = listener;
    }
    public GestureDetector(Context context, OnGestureListener listener,
            Handler handler) { this(context, listener); }
    public boolean onTouchEvent(MotionEvent event) {
        if (listener == null || event == null) return false;
        if (event.getActionMasked() == MotionEvent.ACTION_DOWN)
            return listener.onDown(event);
        if (event.getActionMasked() == MotionEvent.ACTION_UP)
            return listener.onSingleTapUp(event);
        return false;
    }
    public void setOnDoubleTapListener(OnDoubleTapListener listener) {}
    public void setIsLongpressEnabled(boolean enabled) {}
}
