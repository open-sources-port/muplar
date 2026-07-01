package android.window;

import android.view.RemoteAnimationTarget;

public class BackMotionEvent extends BackEvent {
    private final RemoteAnimationTarget departingTarget;
    public BackMotionEvent(float touchX, float touchY, float progress,
            int swipeEdge, RemoteAnimationTarget departingTarget) {
        super(touchX, touchY, progress, swipeEdge);
        this.departingTarget = departingTarget;
    }
    public RemoteAnimationTarget getDepartingAnimationTarget() {
        return departingTarget;
    }
}
