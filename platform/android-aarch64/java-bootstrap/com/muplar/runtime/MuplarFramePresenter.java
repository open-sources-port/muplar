package com.muplar.runtime;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;
import java.io.File;
import java.lang.ref.WeakReference;
import java.util.List;

final class MuplarFramePresenter {
    private static final long LOOP_INTERVAL_MS = 500;

    private static boolean nativeAvailable = true;
    private static long lastFrameUptime;
    private static volatile WeakReference<View> currentRoot;
    private static boolean loopStarted;
    private static volatile int burstFrames;
    private static volatile boolean frameRequested;
    private static volatile boolean hasPresentedAny;
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
        burstFrames = 30;
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
        hasPresentedAny = false;
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
                    if (burstFrames > 0 || dirty || !hasPresentedAny) {
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
        present(root, false);
    }

    static void present(View root, boolean force) {
        String path = System.getenv("MUPLAR_ANDROID_SOFTWARE_FRAME_PATH");
        System.out.println("[Muplar/Window] present entry root=" + root + " force=" + force + " nativeAvail=" + nativeAvailable + " path=" + path);
        if (root == null || !nativeAvailable) {
            return;
        }
        if (path == null || path.isEmpty()) {
            return;
        }
        long now = android.os.SystemClock.uptimeMillis();
        if (!force && now - lastFrameUptime < 16) {
            System.out.println("[Muplar/Window] present skipped due to rate limit dt=" + (now - lastFrameUptime));
            return;
        }
        lastFrameUptime = now;
        if (root.getVisibility() != View.VISIBLE) {
            try {
                root.setVisibility(View.VISIBLE);
            } catch (Throwable ignored) {
            }
        }
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
        System.out.println("[Muplar/Window] present starting w=" + width + " h=" + height);
        try {
            if (reusableBitmap == null || reusableBitmap.isRecycled()
                || reusableBitmap.getWidth() != width
                || reusableBitmap.getHeight() != height) {
                if (reusableBitmap != null && !reusableBitmap.isRecycled()) {
                    try {
                        reusableBitmap.recycle();
                    } catch (Throwable ignored) {
                    }
                }
                reusableBitmap = Bitmap.createBitmap(width, height,
                    Bitmap.Config.ARGB_8888);
            }
            reusableCanvas = new Canvas(reusableBitmap);
            if (System.getenv("MUPLAR_DEBUG_VIEWS") != null) {
                debugDumpViews(root, 0);
            }
            fixBubbleTextViews(root);
            System.out.println("[Muplar/Window] present calling root.draw");
            root.draw(reusableCanvas);
            System.out.println("[Muplar/Window] present calling writeBitmapNative");
            nativeAvailable = writeBitmapNative(reusableBitmap, path);
            if (!nativeAvailable) {
                System.err.println("[Muplar/Window] software frame bridge disabled");
            } else {
                hasPresentedAny = true;
                System.out.println("[Muplar/Window] software frame presented w="
                    + width + " h=" + height + " path=" + path);
            }
        } catch (Throwable error) {
            System.err.println("[Muplar/Window] software frame failed: " +
                error.getClass().getName() + ": " + error.getMessage());
        }
    }


    private static void fixBubbleTextViews(View v) {
        if (v == null) return;
        String cls = v.getClass().getSimpleName();
        if (cls.contains("BubbleTextView")) {
            try {
                java.lang.reflect.Field fIcon = v.getClass().getDeclaredField("mIcon");
                fIcon.setAccessible(true);
                Object icon = fIcon.get(v);
                int iconSize = 144;
                if (icon != null) {
                    try {
                        java.lang.reflect.Field fBitmap = icon.getClass().getDeclaredField("mBitmap");
                        fBitmap.setAccessible(true);
                        Bitmap bmp = (Bitmap) fBitmap.get(icon);
                        if (bmp != null && bmp.getWidth() > 1) {
                            iconSize = bmp.getWidth();
                        }
                    } catch (Throwable ignored) {
                    }
                    android.graphics.drawable.Drawable d = (android.graphics.drawable.Drawable) icon;
                    d.setBounds(0, 0, iconSize, iconSize);
                }
                java.lang.reflect.Field fIconSize = v.getClass().getDeclaredField("mIconSize");
                fIconSize.setAccessible(true);
                fIconSize.setInt(v, iconSize);

                java.lang.reflect.Field fDp = v.getClass().getDeclaredField("mDeviceProfile");
                fDp.setAccessible(true);
                Object dp = fDp.get(v);
                if (dp != null) {
                    try {
                        java.lang.reflect.Field fAllAppsIconSize = dp.getClass().getField("allAppsIconSizePx");
                        fAllAppsIconSize.setInt(dp, iconSize);
                    } catch (Throwable ignored) {
                    }
                    try {
                        java.lang.reflect.Field fCellW = dp.getClass().getField("allAppsCellWidthPx");
                        java.lang.reflect.Field fCellH = dp.getClass().getField("allAppsCellHeightPx");
                        if (fCellW.getInt(dp) < iconSize) {
                            fCellW.setInt(dp, 216);
                        }
                        if (fCellH.getInt(dp) < iconSize + 60) {
                            fCellH.setInt(dp, 216);
                        }
                    } catch (Throwable ignored) {
                    }
                }
                android.view.ViewGroup.LayoutParams lp = v.getLayoutParams();
                if (lp != null && lp.height > 0 && lp.height < iconSize + 60) {
                    lp.height = iconSize + 60;
                }
                if (v instanceof TextView && icon instanceof android.graphics.drawable.Drawable) {
                    TextView tv = (TextView) v;
                    tv.setCompoundDrawables(null, (android.graphics.drawable.Drawable) icon, null, null);
                }
            } catch (Throwable ignored) {
            }
        }
        if (v instanceof ViewGroup) {
            ViewGroup vg = (ViewGroup) v;
            for (int i = 0; i < vg.getChildCount(); i++) {
                fixBubbleTextViews(vg.getChildAt(i));
            }
        }
    }

    private static void debugDumpViews(View v, int depth) {
        if (v == null || depth > 10) return;
        String cls = v.getClass().getSimpleName();
        if (cls.contains("BubbleTextView") || cls.contains("AllApps") || cls.contains("PredictionRow")) {
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < depth; i++) sb.append("  ");
            sb.append(cls).append(" w=").append(v.getWidth()).append(" h=").append(v.getHeight())
              .append(" vis=").append(v.getVisibility());
            if (cls.contains("BubbleTextView")) {
                try {
                    java.lang.reflect.Field fIconSize = v.getClass().getDeclaredField("mIconSize");
                    fIconSize.setAccessible(true);
                    int iconSize = fIconSize.getInt(v);
                    java.lang.reflect.Field fIcon = v.getClass().getDeclaredField("mIcon");
                    fIcon.setAccessible(true);
                    Object icon = fIcon.get(v);
                    Object bounds = icon != null ? ((android.graphics.drawable.Drawable) icon).getBounds() : null;
                    sb.append(" mIconSize=").append(iconSize)
                      .append(" iconBounds=").append(bounds);
                    java.lang.reflect.Field fDp = v.getClass().getDeclaredField("mDeviceProfile");
                    fDp.setAccessible(true);
                    Object dp = fDp.get(v);
                    if (dp != null) {
                        java.lang.reflect.Field fAllAppsIconSize = dp.getClass().getField("allAppsIconSizePx");
                        java.lang.reflect.Field fIconSizePx = dp.getClass().getField("iconSizePx");
                        java.lang.reflect.Field fCellW = dp.getClass().getField("allAppsCellWidthPx");
                        java.lang.reflect.Field fCellH = dp.getClass().getField("allAppsCellHeightPx");
                        sb.append(" dp.allAppsIconSizePx=").append(fAllAppsIconSize.getInt(dp))
                          .append(" dp.iconSizePx=").append(fIconSizePx.getInt(dp))
                          .append(" dp.allAppsCell=").append(fCellW.getInt(dp)).append("x").append(fCellH.getInt(dp));
                    }
                } catch (Throwable t) {
                    sb.append(" err=").append(t.getMessage());
                }
            }
            System.out.println("[Muplar/ViewTree] " + sb.toString());
        }
        if (v instanceof ViewGroup) {
            ViewGroup vg = (ViewGroup) v;
            for (int i = 0; i < vg.getChildCount(); i++) {
                debugDumpViews(vg.getChildAt(i), depth + 1);
            }
        }
    }
}
