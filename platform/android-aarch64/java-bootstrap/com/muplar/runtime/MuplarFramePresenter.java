package com.muplar.runtime;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.view.View;
import java.io.File;

final class MuplarFramePresenter {
    private static boolean nativeAvailable = true;
    private static long lastFrameUptime;

    private MuplarFramePresenter() {
    }

    private static native boolean writeBitmapNative(Bitmap bitmap,
                                                    String path);

    static void schedule(final View root) {
        if (root == null) {
            return;
        }
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
        } catch (Throwable error) {
            present(root);
        }
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
