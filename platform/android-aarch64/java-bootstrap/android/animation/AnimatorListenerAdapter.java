package android.animation;

public abstract class AnimatorListenerAdapter
        implements Animator.AnimatorListener, Animator.AnimatorPauseListener {
    public void onAnimationStart(Animator animation) {}
    public void onAnimationEnd(Animator animation) {}
    public void onAnimationCancel(Animator animation) {}
    public void onAnimationRepeat(Animator animation) {}
    public void onAnimationPause(Animator animation) {}
    public void onAnimationResume(Animator animation) {}
}
