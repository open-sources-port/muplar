package android.animation;

import java.util.ArrayList;

public abstract class Animator implements Cloneable {
    public interface AnimatorListener {
        void onAnimationStart(Animator animation);
        void onAnimationEnd(Animator animation);
        void onAnimationCancel(Animator animation);
        void onAnimationRepeat(Animator animation);
    }
    public interface AnimatorPauseListener {
        void onAnimationPause(Animator animation);
        void onAnimationResume(Animator animation);
    }

    private final ArrayList<AnimatorListener> listeners = new ArrayList<>();
    private long duration = 300;
    private long startDelay;
    private boolean running;
    private TimeInterpolator interpolator;

    public void start() {
        running = true;
        for (AnimatorListener listener : new ArrayList<>(listeners))
            listener.onAnimationStart(this);
        applyFinalValue();
        end();
    }
    public void cancel() {
        if (running)
            for (AnimatorListener listener : new ArrayList<>(listeners))
                listener.onAnimationCancel(this);
        end();
    }
    public void end() {
        boolean notify = running;
        running = false;
        if (notify)
            for (AnimatorListener listener : new ArrayList<>(listeners))
                listener.onAnimationEnd(this);
    }
    public void pause() {}
    public void resume() {}
    public boolean isRunning() { return running; }
    public boolean isStarted() { return running; }
    public Animator setDuration(long duration) { this.duration = duration; return this; }
    public long getDuration() { return duration; }
    public void setStartDelay(long delay) { startDelay = delay; }
    public long getStartDelay() { return startDelay; }
    public void setInterpolator(TimeInterpolator value) { interpolator = value; }
    public TimeInterpolator getInterpolator() { return interpolator; }
    public void addListener(AnimatorListener listener) {
        if (listener != null && !listeners.contains(listener)) listeners.add(listener);
    }
    public void removeListener(AnimatorListener listener) { listeners.remove(listener); }
    public ArrayList<AnimatorListener> getListeners() { return listeners; }
    public void removeAllListeners() { listeners.clear(); }
    protected void applyFinalValue() {}
}
