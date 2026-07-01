package android.window;

public final class BackProgressAnimator {
    public interface ProgressCallback {
        void onProgressUpdate(BackEvent event);
    }
    private ProgressCallback callback;
    private Runnable cancelFinish;
    public BackProgressAnimator() {}
    public void onBackStarted(BackMotionEvent event, ProgressCallback callback) {
        this.callback = callback;
        if (callback != null) callback.onProgressUpdate(event);
    }
    public void onBackProgressed(BackMotionEvent event) {
        if (callback != null) callback.onProgressUpdate(event);
    }
    public void onBackCancelled(Runnable finishCallback) {
        cancelFinish = finishCallback;
        if (finishCallback != null) finishCallback.run();
        reset();
    }
    public void removeOnBackCancelledFinishCallback() { cancelFinish = null; }
    public void reset() { callback = null; cancelFinish = null; }
}
