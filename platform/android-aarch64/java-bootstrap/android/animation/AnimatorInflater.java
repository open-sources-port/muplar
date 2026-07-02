package android.animation;

import android.content.Context;

public final class AnimatorInflater {
    private AnimatorInflater() {}
    public static Animator loadAnimator(Context context, int resourceId) {
        return new AnimatorSet();
    }
}
