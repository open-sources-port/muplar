package android.app;

import android.content.Context;
import android.os.Bundle;
import android.os.IRemoteCallback;
import android.os.IBinder;
import android.view.View;
import android.view.RemoteAnimationAdapter;
import android.window.RemoteTransition;

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
    public static ActivityOptions makeRemoteAnimation(
            RemoteAnimationAdapter adapter, RemoteTransition transition) {
        return new ActivityOptions();
    }
    public Bundle toBundle() { return options; }
    public ActivityOptions setLaunchDisplayId(int displayId) {
        options.putInt("android.activity.launchDisplayId", displayId);
        return this;
    }
    public int getLaunchDisplayId() {
        return options.getInt("android.activity.launchDisplayId", -1);
    }
    public ActivityOptions setLaunchWindowingMode(int mode) {
        options.putInt("android.activity.windowingMode", mode);
        return this;
    }
    public ActivityOptions setSplashScreenStyle(int style) { return this; }
    public void setOnAnimationAbortListener(IRemoteCallback listener) {}
    public void setOnAnimationFinishedListener(IRemoteCallback listener) {}
    public void setLaunchCookie(IBinder cookie) {}
    public ActivityOptions setPendingIntentBackgroundActivityStartMode(int mode) {
        return this;
    }
    public ActivityOptions setPendingIntentCreatorBackgroundActivityStartMode(
            int mode) { return this; }
    public void setSourceInfo(int type, long time) {}
}
