package com.muplar.runtime;

/**
 * Delivers vsync callbacks to android.view.DisplayEventReceiver instances.
 *
 * This environment has no real display HAL/SurfaceFlinger connection to
 * source vsync events from, so the ART shim's native DisplayEventReceiver
 * methods (nativeInit/nativeScheduleVsync) previously did nothing at all:
 * scheduleVsync() was a no-op and the requested callback never fired. Any
 * code that blocks waiting on that callback (Choreographer's first-frame
 * request during onResume(), for one) then waits forever, which ART's own
 * condition-variable fallback turns into a permanent sched_yield/
 * clock_gettime spin rather than a clean block.
 *
 * This schedules a plain ~60fps timer via Handler.postDelayed and calls
 * back into DisplayEventReceiver.dispatchVsync(long, long, int) by
 * reflection, which is enough to unblock that waiting code even without a
 * real display's timing signal behind it.
 */
final class MuplarVsyncScheduler {
    private static final long FRAME_INTERVAL_MS = 16;

    private static volatile int frameCounter;

    private MuplarVsyncScheduler() {
    }

    static void schedule(final Object receiver) {
        if (receiver == null) {
            return;
        }
        try {
            android.os.Looper looper = android.os.Looper.getMainLooper();
            if (looper == null) {
                return;
            }
            new android.os.Handler(looper).postDelayed(new Runnable() {
                @Override public void run() {
                    dispatch(receiver);
                }
            }, FRAME_INTERVAL_MS);
        } catch (Throwable error) {
            System.err.println("[Muplar/Window] vsync schedule failed: " +
                error.getClass().getName() + ": " + error.getMessage());
        }
    }

    private static void dispatch(Object receiver) {
        try {
            java.lang.reflect.Method dispatchVsync =
                findDeclaredMethod(receiver.getClass(), "dispatchVsync",
                    Long.TYPE, Long.TYPE, Integer.TYPE);
            if (dispatchVsync == null) {
                System.err.println(
                    "[Muplar/Window] vsync dispatch failed: no dispatchVsync"
                    + " method found on " + receiver.getClass().getName());
                return;
            }
            dispatchVsync.setAccessible(true);
            dispatchVsync.invoke(receiver,
                Long.valueOf(System.nanoTime()),
                Long.valueOf(0L),
                Integer.valueOf(++frameCounter));
        } catch (Throwable error) {
            System.err.println("[Muplar/Window] vsync dispatch failed: " +
                error.getClass().getName() + ": " + error.getMessage());
        }
    }

    private static java.lang.reflect.Method findDeclaredMethod(Class<?> type,
        String name, Class<?>... paramTypes) {
        while (type != null) {
            try {
                return type.getDeclaredMethod(name, paramTypes);
            } catch (NoSuchMethodException ignored) {
                type = type.getSuperclass();
            }
        }
        return null;
    }
}
