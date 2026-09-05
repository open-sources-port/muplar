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
            android.os.Handler.createAsync(looper).postDelayed(new Runnable() {
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
            long now = System.nanoTime();
            int count = ++frameCounter;
            Class<?> vsyncDataCls = null;
            try {
                vsyncDataCls = Class.forName("android.view.DisplayEventReceiver$VsyncEventData");
            } catch (Throwable ignored) {
            }

            java.lang.reflect.Method method = null;
            if (vsyncDataCls != null) {
                method = findDeclaredMethod(receiver.getClass(), "dispatchVsync",
                    Long.TYPE, Long.TYPE, Integer.TYPE, vsyncDataCls);
                if (method == null) {
                    method = findDeclaredMethod(receiver.getClass(), "onVsync",
                        Long.TYPE, Long.TYPE, Integer.TYPE, vsyncDataCls);
                }
            }
            if (method == null) {
                method = findDeclaredMethod(receiver.getClass(), "dispatchVsync",
                    Long.TYPE, Long.TYPE, Integer.TYPE);
            }
            if (method == null) {
                method = findDeclaredMethod(receiver.getClass(), "onVsync",
                    Long.TYPE, Long.TYPE, Integer.TYPE);
            }

            if (method == null) {
                System.err.println(
                    "[Muplar/Window] vsync dispatch failed: no dispatchVsync/onVsync"
                    + " method found on " + receiver.getClass().getName());
                return;
            }

            method.setAccessible(true);
            Class<?>[] ptypes = method.getParameterTypes();
            if (ptypes.length == 4 && vsyncDataCls != null && ptypes[3].equals(vsyncDataCls)) {
                Object vsyncData = createVsyncEventData(now, count);
                method.invoke(receiver, Long.valueOf(now), Long.valueOf(0L), Integer.valueOf(count), vsyncData);
            } else {
                method.invoke(receiver, Long.valueOf(now), Long.valueOf(0L), Integer.valueOf(count));
            }
        } catch (Throwable error) {
            Throwable cause = error instanceof java.lang.reflect.InvocationTargetException
                ? ((java.lang.reflect.InvocationTargetException) error).getCause()
                : error;
            System.err.println("[Muplar/Window] vsync dispatch failed: " +
                (cause != null ? cause.getClass().getName() + ": " + cause.getMessage() : error.getMessage()));
        }
    }

    private static Object createVsyncEventData(long nowNs, int count) {
        try {
            Class<?> vsyncDataCls = Class.forName("android.view.DisplayEventReceiver$VsyncEventData");
            Class<?> timelineCls = Class.forName("android.view.DisplayEventReceiver$VsyncEventData$FrameTimeline");
            Object timeline = allocateWithoutConstructor(timelineCls);
            if (timeline != null) {
                setField(timeline, "vsyncId", Long.valueOf((long) count));
                setField(timeline, "expectedPresentationTime", Long.valueOf(nowNs + 16666666L));
                setField(timeline, "deadline", Long.valueOf(nowNs + 33333333L));
            }
            Object timelines = java.lang.reflect.Array.newInstance(timelineCls, 7);
            if (timeline != null) {
                java.lang.reflect.Array.set(timelines, 0, timeline);
                for (int i = 1; i < 7; i++) {
                    java.lang.reflect.Array.set(timelines, i, timeline);
                }
            }
            Object data = allocateWithoutConstructor(vsyncDataCls);
            if (data != null) {
                setField(data, "frameInterval", Long.valueOf(16666666L));
                setField(data, "frameTimelines", timelines);
                setField(data, "frameTimelinesLength", Integer.valueOf(1));
                setField(data, "preferredFrameTimelineIndex", Integer.valueOf(0));
            }
            return data;
        } catch (Throwable ignored) {
            return null;
        }
    }

    private static Object allocateWithoutConstructor(Class<?> type) {
        try {
            Class<?> unsafeClass = Class.forName("sun.misc.Unsafe");
            java.lang.reflect.Field field = unsafeClass.getDeclaredField("theUnsafe");
            field.setAccessible(true);
            Object unsafe = field.get(null);
            java.lang.reflect.Method allocateInstance =
                unsafeClass.getMethod("allocateInstance", Class.class);
            return allocateInstance.invoke(unsafe, type);
        } catch (Throwable ignored) {
            return null;
        }
    }

    private static void setField(Object obj, String name, Object value) {
        if (obj == null) return;
        Class<?> cls = obj.getClass();
        while (cls != null && cls != Object.class) {
            try {
                java.lang.reflect.Field f = cls.getDeclaredField(name);
                f.setAccessible(true);
                f.set(obj, value);
                return;
            } catch (Throwable ignored) {
                cls = cls.getSuperclass();
            }
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
