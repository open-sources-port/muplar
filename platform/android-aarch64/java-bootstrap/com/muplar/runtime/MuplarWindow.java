package com.muplar.runtime;

import android.content.Context;
import android.content.res.Configuration;
import android.graphics.drawable.Drawable;
import android.net.Uri;
import android.os.Bundle;
import android.view.InputQueue;
import android.view.KeyEvent;
import android.view.LayoutInflater;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import android.window.OnBackInvokedCallback;
import android.window.OnBackInvokedDispatcher;
import android.widget.FrameLayout;

public final class MuplarWindow extends Window {
    private static final int DISPLAY_WIDTH = 1080;
    private static final int DISPLAY_HEIGHT = 1920;

    private LayoutInflater inflater;
    private OnBackInvokedDispatcher backDispatcher;
    private FrameLayout decorView;
    private View contentView;
    private int statusBarColor;
    private int navigationBarColor;
    private int volumeControlStream;
    private CharSequence title;

    public MuplarWindow(Context context) {
        super(context);
        init(context);
    }

    public static MuplarWindow create(Context context) throws Exception {
        MuplarWindow window =
            (MuplarWindow) allocateWithoutConstructor(MuplarWindow.class);
        window.init(context);
        setWindowField(window, "mContext", context);
        setWindowField(window, "mWindowAttributes",
            new WindowManager.LayoutParams());
        int features = (1 << Window.FEATURE_OPTIONS_PANEL)
            | (1 << Window.FEATURE_CONTEXT_MENU);
        setWindowField(window, "mFeatures", Integer.valueOf(features));
        setWindowField(window, "mLocalFeatures", Integer.valueOf(features));
        return window;
    }

    private void init(Context context) {
        inflater = new MuplarLayoutInflater(context);
        decorView = new FrameLayout(context);
        decorView.setLayoutParams(new ViewGroup.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.MATCH_PARENT));
        layoutDecor();
        backDispatcher = new OnBackInvokedDispatcher() {
            @Override
            public void registerOnBackInvokedCallback(
                int priority, OnBackInvokedCallback callback) {
            }

            @Override
            public void unregisterOnBackInvokedCallback(
                OnBackInvokedCallback callback) {
            }
        };
    }

    @Override
    public void addContentView(View view, ViewGroup.LayoutParams params) {
        if (view == null) {
            return;
        }
        contentView = view;
        decorView.addView(view, normalizedParams(params));
        layoutDecor();
    }

    @Override
    public void closeAllPanels() {
    }

    @Override
    public void closePanel(int featureId) {
    }

    @Override
    public View getCurrentFocus() {
        return contentView != null ? contentView : decorView;
    }

    @Override
    public View getDecorView() {
        return decorView;
    }

    @Override
    public LayoutInflater getLayoutInflater() {
        return inflater;
    }

    @Override
    public int getNavigationBarColor() {
        return navigationBarColor;
    }

    @Override
    public OnBackInvokedDispatcher getOnBackInvokedDispatcher() {
        return backDispatcher;
    }

    @Override
    public int getStatusBarColor() {
        return statusBarColor;
    }

    @Override
    public int getVolumeControlStream() {
        return volumeControlStream;
    }

    @Override
    public void invalidatePanelMenu(int featureId) {
    }

    @Override
    public boolean isFloating() {
        return false;
    }

    @Override
    public boolean isShortcutKey(int keyCode, KeyEvent event) {
        return false;
    }

    @Override
    protected void onActive() {
    }

    @Override
    public void onConfigurationChanged(Configuration newConfig) {
    }

    @Override
    public void openPanel(int featureId, KeyEvent event) {
    }

    @Override
    public View peekDecorView() {
        return decorView;
    }

    @Override
    public boolean performContextMenuIdentifierAction(int id, int flags) {
        return false;
    }

    @Override
    public boolean performPanelIdentifierAction(
        int featureId, int id, int flags) {
        return false;
    }

    @Override
    public boolean performPanelShortcut(
        int featureId, int keyCode, KeyEvent event, int flags) {
        return false;
    }

    @Override
    public void restoreHierarchyState(Bundle savedInstanceState) {
    }

    @Override
    public Bundle saveHierarchyState() {
        return new Bundle();
    }

    @Override
    public void setBackgroundDrawable(Drawable drawable) {
    }

    @Override
    public void setChildDrawable(int featureId, Drawable drawable) {
    }

    @Override
    public void setChildInt(int featureId, int value) {
    }

    @Override
    public void setContentView(int layoutResId) {
        System.out.println("[Muplar/Window] setContentView(int) called: " + layoutResId);
        setContentView(inflater.inflate(layoutResId, null));
    }

    @Override
    public void setContentView(View view) {
        System.out.println("[Muplar/Window] setContentView(View) called: " + view);
        setContentViewInternal(view, null);
    }

    @Override
    public void setContentView(View view, ViewGroup.LayoutParams params) {
        System.out.println("[Muplar/Window] setContentView(View, LayoutParams) called: " + view);
        setContentViewInternal(view, params);
    }

    private void setContentViewInternal(View view, ViewGroup.LayoutParams params) {
        if (view == null) {
            return;
        }
        contentView = view;
        decorView.removeAllViews();
        decorView.addView(view, normalizedParams(params));
        layoutDecor();
        MuplarFramePresenter.schedule(decorView);
        System.out.println("[Muplar/Window] decor laid out: decor="
            + decorView.getWidth() + "x" + decorView.getHeight()
            + " content=" + view.getWidth() + "x" + view.getHeight());
    }

    private static ViewGroup.LayoutParams normalizedParams(
        ViewGroup.LayoutParams params) {
        if (params != null) {
            if (params.width <= 0) {
                params.width = ViewGroup.LayoutParams.MATCH_PARENT;
            }
            if (params.height <= 0) {
                params.height = ViewGroup.LayoutParams.MATCH_PARENT;
            }
            return params;
        }
        return new FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.MATCH_PARENT);
    }

    private void layoutDecor() {
        if (decorView == null) {
            return;
        }
        int widthSpec = View.MeasureSpec.makeMeasureSpec(
            DISPLAY_WIDTH, View.MeasureSpec.EXACTLY);
        int heightSpec = View.MeasureSpec.makeMeasureSpec(
            DISPLAY_HEIGHT, View.MeasureSpec.EXACTLY);
        decorView.measure(widthSpec, heightSpec);
        decorView.layout(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    }

    @Override
    public void setDecorCaptionShade(int decorCaptionShade) {
    }

    @Override
    public void setFeatureDrawable(int featureId, Drawable drawable) {
    }

    @Override
    public void setFeatureDrawableAlpha(int featureId, int alpha) {
    }

    @Override
    public void setFeatureDrawableResource(int featureId, int resId) {
    }

    @Override
    public void setFeatureDrawableUri(int featureId, Uri uri) {
    }

    @Override
    public void setFeatureInt(int featureId, int value) {
    }

    @Override
    public void setNavigationBarColor(int color) {
        navigationBarColor = color;
    }

    @Override
    public void setResizingCaptionDrawable(Drawable drawable) {
    }

    @Override
    public void setStatusBarColor(int color) {
        statusBarColor = color;
    }

    @Override
    public void setTitle(CharSequence title) {
        this.title = title;
        getAttributes().setTitle(title);
    }

    @Override
    public void setTitleColor(int textColor) {
    }

    @Override
    public void setVolumeControlStream(int streamType) {
        volumeControlStream = streamType;
    }

    @Override
    public boolean superDispatchGenericMotionEvent(MotionEvent event) {
        boolean handled =
            decorView != null && decorView.dispatchGenericMotionEvent(event);
        MuplarFramePresenter.schedule(decorView);
        return handled;
    }

    @Override
    public boolean superDispatchKeyEvent(KeyEvent event) {
        boolean handled = decorView != null && decorView.dispatchKeyEvent(event);
        MuplarFramePresenter.schedule(decorView);
        return handled;
    }

    @Override
    public boolean superDispatchKeyShortcutEvent(KeyEvent event) {
        boolean handled =
            decorView != null && decorView.dispatchKeyShortcutEvent(event);
        MuplarFramePresenter.schedule(decorView);
        return handled;
    }

    @Override
    public boolean superDispatchTouchEvent(MotionEvent event) {
        boolean handled =
            decorView != null && decorView.dispatchTouchEvent(event);
        MuplarFramePresenter.schedule(decorView);
        return handled;
    }

    @Override
    public boolean superDispatchTrackballEvent(MotionEvent event) {
        boolean handled =
            decorView != null && decorView.dispatchTrackballEvent(event);
        MuplarFramePresenter.schedule(decorView);
        return handled;
    }

    @Override
    public void takeInputQueue(InputQueue.Callback callback) {
    }

    @Override
    public void takeKeyEvents(boolean get) {
    }

    @Override
    public void takeSurface(SurfaceHolder.Callback2 callback) {
    }

    @Override
    public void togglePanel(int featureId, KeyEvent event) {
    }

    private static Object allocateWithoutConstructor(Class<?> type)
        throws Exception {
        Class<?> unsafeClass = Class.forName("sun.misc.Unsafe");
        java.lang.reflect.Field field =
            unsafeClass.getDeclaredField("theUnsafe");
        field.setAccessible(true);
        Object unsafe = field.get(null);
        java.lang.reflect.Method allocateInstance =
            unsafeClass.getMethod("allocateInstance", Class.class);
        return allocateInstance.invoke(unsafe, type);
    }

    private static void setWindowField(Object target, String name, Object value)
        throws Exception {
        java.lang.reflect.Field field = Window.class.getDeclaredField(name);
        field.setAccessible(true);
        field.set(target, value);
    }
}
