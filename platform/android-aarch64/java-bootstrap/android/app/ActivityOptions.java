package android.app;

import android.content.Context;
import android.os.Bundle;
import android.view.View;

public class ActivityOptions {
    private final Bundle options = new Bundle();
    public static ActivityOptions makeBasic() { return new ActivityOptions(); }
    public static ActivityOptions makeCustomAnimation(
            Context context, int enterResId, int exitResId) {
        return new ActivityOptions();
    }
    public static ActivityOptions makeScaleUpAnimation(View source,
            int startX, int startY, int width, int height) {
        return new ActivityOptions();
    }
    public Bundle toBundle() { return options; }
    public void setLaunchDisplayId(int displayId) {
        options.putInt("android.activity.launchDisplayId", displayId);
    }
    public int getLaunchDisplayId() {
        return options.getInt("android.activity.launchDisplayId", -1);
    }
    public void setLaunchWindowingMode(int mode) {
        options.putInt("android.activity.windowingMode", mode);
    }
    public void setSplashScreenStyle(int style) {}
    public ActivityOptions setPendingIntentBackgroundActivityStartMode(int mode) {
        return this;
    }
    public ActivityOptions setPendingIntentCreatorBackgroundActivityStartMode(
            int mode) { return this; }
}
