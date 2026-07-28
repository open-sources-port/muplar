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
        try {
            android.os.Looper looper = android.os.Looper.getMainLooper();
            if (looper == null) {
                present(root);
                return;
            }
            android.os.Handler handler = new android.os.Handler(looper);
            handler.post(new Runnable() {
                @Override public void run() {
                    present(root);
                }
            });
            handler.postDelayed(new Runnable() {
                @Override public void run() {
                    present(root);
                }
            }, 48);
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
    }

    private static void startLoop(final android.os.Handler handler) {
        if (loopStarted) {
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
                    present(root);
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
        int width = Math.max(1, root.getWidth());
        int height = Math.max(1, root.getHeight());
        Bitmap bitmap = null;
        try {
            File parent = new File(path).getParentFile();
            if (parent != null) {
                parent.mkdirs();
            }
            bitmap = Bitmap.createBitmap(width, height,
                Bitmap.Config.ARGB_8888);
            Canvas canvas = new Canvas(bitmap);
            canvas.drawColor(Color.rgb(238, 238, 238));
            root.draw(canvas);
            nativeAvailable = writeBitmapNative(bitmap, path);
            if (!nativeAvailable) {
                System.err.println("[Muplar/Window] software frame bridge disabled");
            } else {
                System.out.println("[Muplar/Window] software frame presented w="
                    + width + " h=" + height + " path=" + path);
            }
        } catch (Throwable error) {
            System.err.println("[Muplar/Window] software frame failed: " +
                error.getClass().getName() + ": " + error.getMessage());
        } finally {
            if (bitmap != null) {
                try {
                    bitmap.recycle();
                } catch (Throwable ignored) {
                }
            }
        }
    }
}
