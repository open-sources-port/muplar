package com.muplar.runtime;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.view.View;
import java.io.File;
import java.lang.ref.WeakReference;

final class MuplarFramePresenter {
    private static final long LOOP_INTERVAL_MS = 200;

    private static boolean nativeAvailable = true;
    private static long lastFrameUptime;
    private static volatile WeakReference<View> currentRoot;
    private static boolean loopStarted;
    private static volatile int burstFrames;
    private static Bitmap reusableBitmap;
    private static Canvas reusableCanvas;

    private MuplarFramePresenter() {
    }

    private static native boolean writeBitmapNative(Bitmap bitmap,
                                                    String path);

    /**
     * Requests a frame for {@code root} and, the first time this is ever
     * called, starts a persistent low-frequency loop that keeps
     * re-presenting whatever the most recently scheduled root is. Real
     * Android apps get continuous frame delivery from
     * Choreographer/RenderThread; this bridge has neither, so without the
     * loop the device window only ever shows the single frame captured at
     * the moment of the last device action (focus/tab-switch/input),
     * which is often stale or premature (e.g. before Launcher3's async
     * model load finishes laying out icons).
     */
    static void schedule(final View root) {
        if (root == null) {
            return;
        }
        currentRoot = new WeakReference<>(root);
        burstFrames = 3;
        try {
            android.os.Looper looper = android.os.Looper.getMainLooper();
            if (looper == null) {
                present(root);
                return;
            }
            android.os.Handler handler = android.os.Handler.createAsync(looper);
            handler.post(new Runnable() {
                @Override public void run() {
                    present(root);
                }
            });
            startLoop(handler);
        } catch (Throwable error) {
            present(root);
        }
    }

    /**
     * Stops the presenter loop from re-drawing whatever root it last had:
     * used when a tab is removed and no other tab becomes active, so the
     * device window doesn't keep showing a since-destroyed activity's
     * decor view forever.
     */
    static void clear() {
        currentRoot = null;
        burstFrames = 0;
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
        System.out.println("[Muplar/Window] frame loop started interval="
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
        int width = root.getWidth();
        int height = root.getHeight();
        if (width <= 0 || height <= 0) {
            width = 1080;
            height = 1920;
            try {
                int wSpec = View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY);
                int hSpec = View.MeasureSpec.makeMeasureSpec(height, View.MeasureSpec.EXACTLY);
                root.measure(wSpec, hSpec);
                root.layout(0, 0, width, height);
            } catch (Throwable ignored) {
            }
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
            reusableCanvas.drawColor(Color.rgb(238, 238, 238));
            root.draw(reusableCanvas);
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
}
