package android.view.animation;

public class Animation {
    public interface AnimationListener {
        void onAnimationStart(Animation animation);
        void onAnimationEnd(Animation animation);
        void onAnimationRepeat(Animation animation);
    }
    private AnimationListener listener;
    public void setAnimationListener(AnimationListener value) { listener = value; }
    public AnimationListener getAnimationListener() { return listener; }
    public void start() {
        if (listener != null) {
            listener.onAnimationStart(this);
            listener.onAnimationEnd(this);
        }
    }
    public void cancel() {}
    public void reset() {}
}
