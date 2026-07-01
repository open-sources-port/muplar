package android.view;

public class RemoteAnimationAdapter {
    public final IRemoteAnimationRunner runner;
    public final long duration;
    public final long statusBarTransitionDelay;
    public RemoteAnimationAdapter(IRemoteAnimationRunner runner, long duration,
            long statusBarTransitionDelay) {
        this.runner = runner;
        this.duration = duration;
        this.statusBarTransitionDelay = statusBarTransitionDelay;
    }
}
