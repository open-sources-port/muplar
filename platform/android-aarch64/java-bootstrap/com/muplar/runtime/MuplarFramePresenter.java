package com.muplar.runtime;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.view.View;
import java.io.File;
import java.lang.ref.WeakReference;

final class MuplarFramePresenter {
    private static final long LOOP_INTERVAL_MS = 500;

    private static boolean nativeAvailable = true;
    private static long lastFrameUptime;
    private static volatile WeakReference<View> currentRoot;
    private static boolean loopStarted;
    private static volatile int burstFrames;
    private static volatile boolean frameRequested;
    private static Bitmap reusableBitmap;
    private static Canvas reusableCanvas;

    private MuplarFramePresenter() {
    }

    private static native boolean writeBitmapNative(Bitmap bitmap,
                                                    String path);

    /**
     * Schedules presentation for {@code root}, registers ViewTreeObserver
     * listeners for event-driven frame delivery upon invalidations/draws,
     * and starts a low-frequency idle heartbeat fallback.
     */
    static void schedule(final View root) {
        if (root == null) {
            return;
        }
        currentRoot = new WeakReference<>(root);
        burstFrames = 3;
        try {
            java.lang.reflect.Method getViewRootImpl =
                View.class.getDeclaredMethod("getViewRootImpl");
            getViewRootImpl.setAccessible(true);
            Object vri = getViewRootImpl.invoke(root);
            if (vri != null) {
                java.lang.reflect.Method dispatchAppVis =
                    vri.getClass().getMethod("dispatchAppVisibility", boolean.class);
                dispatchAppVis.invoke(vri, Boolean.TRUE);
            }
        } catch (Throwable ignored) {
        }
        try {
            android.os.Looper looper = android.os.Looper.getMainLooper();
            if (looper == null) {
                present(root);
                return;
            }
            final android.os.Handler handler = android.os.Handler.createAsync(looper);
            handler.post(new Runnable() {
                @Override public void run() {
                    present(root);
                    attachObserver(root, handler);
                }
            });
            startLoop(handler);
        } catch (Throwable error) {
            present(root);
        }
    }

    /**
     * Immediately schedules a frame presentation for the currently active root,
     * e.g. following input event dispatch or tab transitions.
     */
    static void requestImmediateFrame() {
        WeakReference<View> ref = currentRoot;
        View root = ref != null ? ref.get() : null;
        if (root != null) {
            try {
                android.os.Looper looper = android.os.Looper.getMainLooper();
                if (looper != null) {
                    android.os.Handler handler = android.os.Handler.createAsync(looper);
                    requestFrame(root, handler);
                } else {
                    present(root);
                }
            } catch (Throwable ignored) {
                present(root);
            }
        }
    }

    /**
     * Captures several frames after state changes that may animate without
     * going through a real hardware renderer invalidation path.
     */
    static void requestBurst() {
        WeakReference<View> ref = currentRoot;
        View root = ref != null ? ref.get() : null;
        if (root == null) {
            return;
        }
        burstFrames = Math.max(burstFrames, 12);
        requestImmediateFrame();
    }

    private static void attachObserver(final View root, final android.os.Handler handler) {
        try {
            android.view.ViewTreeObserver vto = root.getViewTreeObserver();
            if (vto == null || !vto.isAlive()) {
                return;
            }
            vto.addOnDrawListener(new android.view.ViewTreeObserver.OnDrawListener() {
                @Override public void onDraw() {
                    requestFrame(root, handler);
                }
            });
            vto.addOnGlobalLayoutListener(new android.view.ViewTreeObserver.OnGlobalLayoutListener() {
                @Override public void onGlobalLayout() {
                    requestFrame(root, handler);
                }
            });
        } catch (Throwable ignored) {
        }
    }

    private static void requestFrame(final View root, final android.os.Handler handler) {
        if (root == null || frameRequested) {
            return;
        }
        frameRequested = true;
        handler.post(new Runnable() {
            @Override public void run() {
                frameRequested = false;
                present(root);
            }
        });
    }

    /**
     * Stops the presenter loop from re-drawing whatever root it last had.
     */
    static void clear() {
        currentRoot = null;
        burstFrames = 0;
        frameRequested = false;
        if (reusableBitmap != null && !reusableBitmap.isRecycled()) {
            try {
                reusableBitmap.recycle();
            } catch (Throwable ignored) {
            }
            reusableBitmap = null;
            reusableCanvas = null;
        }
    }

    private static void startLoop(final android.os.Handler handler) {
        if (loopStarted) {
            return;
        }
        String path = System.getenv("MUPLAR_ANDROID_SOFTWARE_FRAME_PATH");
        if (path == null || path.isEmpty()) {
            return;
        }
        loopStarted = true;
        System.out.println("[Muplar/Window] frame presenter event-driven loop active interval="
            + LOOP_INTERVAL_MS);
        handler.postDelayed(new Runnable() {
            @Override public void run() {
                WeakReference<View> ref = currentRoot;
                View root = ref != null ? ref.get() : null;
                if (root != null) {
                    boolean dirty = false;
                    try {
                        dirty = root.isDirty();
                    } catch (Throwable ignored) {
                    }
                    if (burstFrames > 0 || dirty) {
                        if (burstFrames > 0) {
                            burstFrames--;
                        }
                        present(root);
                    }
                }
                handler.postDelayed(this, LOOP_INTERVAL_MS);
            }
        }, LOOP_INTERVAL_MS);
    }

    static void present(View root) {
        if (root == null || !nativeAvailable) {
            return;
        }
        String path = System.getenv("MUPLAR_ANDROID_SOFTWARE_FRAME_PATH");
        if (path == null || path.isEmpty()) {
            return;
        }
        long now = android.os.SystemClock.uptimeMillis();
        if (now - lastFrameUptime < 16) {
            return;
        }
        lastFrameUptime = now;
        if (root.getVisibility() != View.VISIBLE) {
            try {
                root.setVisibility(View.VISIBLE);
            } catch (Throwable ignored) {
            }
        }
        int width = root.getWidth() > 0 ? root.getWidth() : 1080;
        int height = root.getHeight() > 0 ? root.getHeight() : 1920;
        try {
            root.forceLayout();
            int wSpec = View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY);
            int hSpec = View.MeasureSpec.makeMeasureSpec(height, View.MeasureSpec.EXACTLY);
            root.measure(wSpec, hSpec);
            root.layout(0, 0, width, height);
        } catch (Throwable error) {
            System.err.println("[Muplar/Window] layout error: " + error);
        }
        width = Math.max(1, root.getWidth());
        height = Math.max(1, root.getHeight());
        try {
            File parent = new File(path).getParentFile();
            if (parent != null) {
                parent.mkdirs();
            }
            if (reusableBitmap == null || reusableBitmap.getWidth() != width ||
                reusableBitmap.getHeight() != height || reusableBitmap.isRecycled()) {
                if (reusableBitmap != null && !reusableBitmap.isRecycled()) {
                    try {
                        reusableBitmap.recycle();
                    } catch (Throwable ignored) {
                    }
                }
                reusableBitmap = Bitmap.createBitmap(width, height,
                    Bitmap.Config.ARGB_8888);
                reusableCanvas = new Canvas(reusableBitmap);
            }
            if (reusableCanvas == null) {
                reusableCanvas = new Canvas(reusableBitmap);
            }
            int saveCount = reusableCanvas.save();
            reusableCanvas.drawColor(Color.rgb(238, 238, 238));
            root.draw(reusableCanvas);
            reusableCanvas.restoreToCount(saveCount);
            if (isFallbackOnlyFrame(reusableBitmap)) {
                return;
            }
            nativeAvailable = writeBitmapNative(reusableBitmap, path);
            if (!nativeAvailable) {
                System.err.println("[Muplar/Window] software frame bridge disabled");
            } else {
                System.out.println("[Muplar/Window] software frame presented w="
                    + width + " h=" + height + " path=" + path);
            }
        } catch (Throwable error) {
            System.err.println("[Muplar/Window] software frame failed: " +
                error.getClass().getName() + ": " + error.getMessage());
        }
    }

    private static boolean isFallbackOnlyFrame(Bitmap bitmap) {
        if (bitmap == null || bitmap.isRecycled()) {
            return true;
        }
        try {
            int width = bitmap.getWidth();
            int height = bitmap.getHeight();
            if (width <= 0 || height <= 0) {
                return true;
            }
            int fallback = Color.rgb(238, 238, 238);
            int[] xs = new int[] { 0, width / 4, width / 2, (width * 3) / 4, width - 1 };
            int[] ys = new int[] { 0, height / 4, height / 2, (height * 3) / 4, height - 1 };
            for (int y : ys) {
                for (int x : xs) {
                    if ((bitmap.getPixel(x, y) & 0x00ffffff) !=
                        (fallback & 0x00ffffff)) {
                        return false;
                    }
                }
            }
            return true;
        } catch (Throwable ignored) {
            return false;
        }
    }

}
