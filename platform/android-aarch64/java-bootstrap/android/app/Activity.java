package android.app;

import android.content.Context;
import android.content.Intent;
import android.content.ComponentName;
import android.content.res.Configuration;
import android.os.Bundle;
import android.view.View;
import android.view.LayoutInflater;
import android.view.RemoteAnimationDefinition;
import android.view.Window;
import android.view.WindowManager;
import android.os.Binder;
import android.os.IBinder;
import android.os.Handler;
import android.window.OnBackInvokedDispatcher;
import android.window.OnBackInvokedCallback;
import java.util.TreeMap;
import com.muplar.runtime.HostUi;
import com.muplar.runtime.IntentDispatcher;

public class Activity extends Context {
    public static final int DEFAULT_KEYS_DISABLE = 0;
    public static final int DEFAULT_KEYS_DIALER = 1;
    public static final int DEFAULT_KEYS_SHORTCUT = 2;
    public static final int DEFAULT_KEYS_SEARCH_LOCAL = 3;
    public static final int DEFAULT_KEYS_SEARCH_GLOBAL = 4;
    private String title = "Android";
    private View contentView;
    private final OnBackInvokedDispatcher backDispatcher = new BackDispatcher();
    private RemoteAnimationDefinition remoteAnimations;
    private final IApplicationThread applicationThread = new IApplicationThread() {
        private final Binder binder = new Binder();
        public IBinder asBinder() { return binder; }
    };
    private final Window window = new Window(this);
    private int defaultKeyMode;
    private int requestedOrientation = -1;
    private final Handler mainThreadHandler = new Handler();

    public Activity() {}

    public Application getApplication() {
        Context context = getApplicationContext();
        return context instanceof Application ? (Application) context : null;
    }
    public ComponentName getComponentName() {
        return new ComponentName(getPackageName(), getClass().getName());
    }
    public Window getWindow() { return window; }
    public void setDefaultKeyMode(int mode) { defaultKeyMode = mode; }
    public void setRequestedOrientation(int orientation) {
        requestedOrientation = orientation;
    }
    public int getRequestedOrientation() { return requestedOrientation; }
    public Handler getMainThreadHandler() { return mainThreadHandler; }
    public WindowManager getWindowManager() {
        return (WindowManager) getSystemService(WINDOW_SERVICE);
    }

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
    public boolean isInMultiWindowMode() { return false; }
    public boolean isInPictureInPictureMode() { return false; }
    public Object getLastNonConfigurationInstance() { return null; }
    public Object onRetainNonConfigurationInstance() { return null; }

    public void setTitle(CharSequence title) {
        this.title = title == null ? "Android" : title.toString();
    }
    public void setTitle(int resourceId) { setTitle(getText(resourceId)); }

    public void setTheme(int resourceId) {
        getTheme().applyStyle(resourceId, true);
    }

    public void setContentView(View view) {
        contentView = view;
        HostUi.showActivity(this, title, view);
    }

    public void setContentView(int layoutResourceId) {
        setContentView(LayoutInflater.from(this).inflate(layoutResourceId, null));
    }

    public View findViewById(int id) {
        return contentView == null ? null : contentView.findViewById(id);
    }
    public boolean dispatchTouchEvent(android.view.MotionEvent event) {
        return contentView != null && contentView.dispatchTouchEvent(event);
    }

    public void finish() {
        HostUi.finishActivity(this);
    }

    public void startActivity(Intent intent) {
        if (intent != null && (
                "com.android.launcher3".equals(intent.getComponentPackage()) ||
                (intent.getCategories() != null && intent.getCategories().contains(Intent.CATEGORY_HOME))
           )) {
            if (!"com.android.launcher3".equals(getPackageName())) {
                finish();
            }
            return;
        }
        if (intent != null && getPackageName().equals(intent.getComponentPackage()) &&
                (getPackageName() + ".proxy.ProxyActivityStarter").equals(
                    intent.getComponentClass())) {
            return;
        }
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

    @Override public void startActivity(Intent intent, android.os.Bundle options) {
        startActivity(intent);
    }

    public void runOnUiThread(Runnable action) {
        if (action != null) HostUi.runOnUiThread(action);
    }

    public OnBackInvokedDispatcher getOnBackInvokedDispatcher() {
        return backDispatcher;
    }
    public void onBackPressed() {
        OnBackInvokedCallback callback = null;
        if (backDispatcher instanceof BackDispatcher) {
            TreeMap<Integer, OnBackInvokedCallback> callbacks = ((BackDispatcher) backDispatcher).callbacks;
            if (!callbacks.isEmpty()) {
                callback = callbacks.lastEntry().getValue();
            }
        }
        if (callback != null) {
            callback.onBackInvoked();
        } else {
            finish();
        }
    }
    public void registerRemoteAnimations(RemoteAnimationDefinition definition) {
        remoteAnimations = definition;
    }
    public void unregisterRemoteAnimations() { remoteAnimations = null; }
    public IApplicationThread getIApplicationThread() { return applicationThread; }

    private static final class BackDispatcher implements OnBackInvokedDispatcher {
        private final TreeMap<Integer, OnBackInvokedCallback> callbacks =
            new TreeMap<>();
        public void registerOnBackInvokedCallback(
                int priority, OnBackInvokedCallback callback) {
            if (callback != null) callbacks.put(priority, callback);
        }
        public void unregisterOnBackInvokedCallback(OnBackInvokedCallback callback) {
            callbacks.values().removeIf(value -> value == callback);
        }
    }

    public final void dispatchStartAndResume() {
        onStart();
        onResume();
        mainThreadHandler.postDelayed(new Runnable() {
            @Override public void run() {
                HostUi.runOnUiThread(new Runnable() {
                    @Override public void run() {
                        HostUi.materializeAdapters(contentView);
                    }
                });
            }
        }, 750);
        mainThreadHandler.postDelayed(new Runnable() {
            @Override public void run() {
                HostUi.runOnUiThread(new Runnable() {
                    @Override public void run() {
                        HostUi.materializeAdapters(contentView);
                    }
                });
            }
        }, 3000);
    }
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
