package com.muplar.runtime;

import android.graphics.Bitmap;

public final class MuplarGraphics {
    private MuplarGraphics() {
    }

    public static native boolean presentBitmap(long nativePtr, int width, int height);

    public static Bitmap createBitmap(int width, int height) {
        try {
            Bitmap bitmap = constructBitmap();
            setFieldIfPresent(bitmap, "mWidth", Integer.valueOf(Math.max(1, width)));
            setFieldIfPresent(bitmap, "mHeight", Integer.valueOf(Math.max(1, height)));
            setFieldIfPresent(bitmap, "mDensity", Integer.valueOf(320));
            setFieldIfPresent(bitmap, "mNativePtr", Long.valueOf(1L));
            setFieldIfPresent(bitmap, "mRecycled", Boolean.FALSE);
            return bitmap;
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] synthetic bitmap create failed: "
                + t.getClass().getName() + ": " + t.getMessage());
            return null;
        }
    }

    private static Bitmap constructBitmap() throws Exception {
        try {
            java.lang.reflect.Constructor<Bitmap> ctor =
                Bitmap.class.getDeclaredConstructor();
            ctor.setAccessible(true);
            return ctor.newInstance();
        } catch (Throwable ignored) {
            Object allocated = allocateWithoutConstructor(Bitmap.class);
            if (allocated instanceof Bitmap) {
                return (Bitmap) allocated;
            }
            throw new IllegalStateException("unable to allocate Bitmap");
        }
    }

    private static void setFieldIfPresent(Object target, String name, Object value) {
        try {
            java.lang.reflect.Field field = target.getClass().getDeclaredField(name);
            field.setAccessible(true);
            field.set(target, value);
        } catch (Throwable ignored) {
        }
    }

    private static Object allocateWithoutConstructor(Class<?> type) {
        try {
            Class<?> unsafeClass = Class.forName("sun.misc.Unsafe");
            java.lang.reflect.Field field =
                unsafeClass.getDeclaredField("theUnsafe");
            field.setAccessible(true);
            Object unsafe = field.get(null);
            java.lang.reflect.Method allocateInstance =
                unsafeClass.getMethod("allocateInstance", Class.class);
            return allocateInstance.invoke(unsafe, type);
        } catch (Throwable ignored) {
            return null;
        }
    }
}
