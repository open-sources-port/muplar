package android.app.prediction;

public final class AppTargetEvent {
    public static final int ACTION_LAUNCH = 1;
    public static final int ACTION_DISMISS = 2;
    public static final int ACTION_PIN = 3;
    public static final int ACTION_UNPIN = 4;
    private final AppTarget target;
    private final int action;
    private final String launchLocation;

    private AppTargetEvent(Builder builder) {
        target = builder.target;
        action = builder.action;
        launchLocation = builder.launchLocation;
    }
    public AppTarget getTarget() { return target; }
    public int getAction() { return action; }
    public String getLaunchLocation() { return launchLocation; }

    public static final class Builder {
        private final AppTarget target;
        private final int action;
        private String launchLocation;
        public Builder(AppTarget target, int action) {
            this.target = target;
            this.action = action;
        }
        public Builder setLaunchLocation(String value) {
            launchLocation = value; return this;
        }
        public AppTargetEvent build() { return new AppTargetEvent(this); }
    }
}
