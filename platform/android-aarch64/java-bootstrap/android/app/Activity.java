package android.app;

import android.content.Context;
import android.content.Intent;
import android.content.res.Configuration;
import android.os.Bundle;
import android.view.View;
import android.view.LayoutInflater;
import com.muplar.runtime.HostUi;
import com.muplar.runtime.IntentDispatcher;

public class Activity extends Context {
    private String title = "Android";
    private View contentView;

    public Activity() {}

    protected void onCreate(Bundle savedInstanceState) {
        // stub
    }
    protected void onStart() {}
    protected void onResume() {}
    protected void onPause() {}
    protected void onStop() {}
    protected void onDestroy() {}
    public void onWindowFocusChanged(boolean hasFocus) {}
    public void onConfigurationChanged(Configuration newConfig) {}

    public void setTitle(CharSequence title) {
        this.title = title == null ? "Android" : title.toString();
    }

    public void setTheme(int resourceId) {
        getTheme().applyStyle(resourceId, true);
    }

    public void setContentView(View view) {
        contentView = view;
        HostUi.showActivity(this, title, view == null ? null : view.getPeer());
    }

    public void setContentView(int layoutResourceId) {
        setContentView(LayoutInflater.from(this).inflate(layoutResourceId, null));
    }

    public View findViewById(int id) {
        return contentView == null ? null : contentView.findViewById(id);
    }

    public void finish() {
        HostUi.finishActivity(this);
    }

    public void startActivity(Intent intent) {
        onPause();
        final Process child = IntentDispatcher.launch(intent, getPackageManager());
        Thread waiter = new Thread(new Runnable() {
            @Override public void run() {
                try { child.waitFor(); }
                catch (InterruptedException error) {
                    Thread.currentThread().interrupt();
                }
                runOnUiThread(new Runnable() {
                    @Override public void run() { onResume(); }
                });
            }
        }, "muplar-activity-result");
        waiter.setDaemon(true);
        waiter.start();
    }

    public void runOnUiThread(Runnable action) {
        if (action != null) HostUi.runOnUiThread(action);
    }

    public final void dispatchStartAndResume() { onStart(); onResume(); }
    public final void dispatchWindowFocusChanged(boolean focused) {
        onWindowFocusChanged(focused);
    }
    public final void dispatchConfigurationChanged(Configuration configuration) {
        onConfigurationChanged(configuration);
    }
    public final void dispatchClose() {
        onPause(); onStop(); onDestroy();
    }
}
