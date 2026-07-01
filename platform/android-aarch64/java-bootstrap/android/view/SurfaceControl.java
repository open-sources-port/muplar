package android.view;

import android.graphics.Matrix;
import android.graphics.Rect;
import android.os.Binder;
import android.os.IBinder;

public class SurfaceControl {
    private boolean valid = true;
    public boolean isValid() { return valid; }
    public void release() { valid = false; }

    public static class Transaction implements AutoCloseable {
        private static IBinder defaultApplyToken = new Binder();
        public static IBinder getDefaultApplyToken() { return defaultApplyToken; }
        public static void setDefaultApplyToken(IBinder token) {
            defaultApplyToken = token;
        }
        public Transaction show(SurfaceControl surface) { return this; }
        public Transaction hide(SurfaceControl surface) { return this; }
        public Transaction setAlpha(SurfaceControl surface, float alpha) { return this; }
        public Transaction setColor(SurfaceControl surface, float[] color) { return this; }
        public Transaction setMatrix(SurfaceControl surface, Matrix matrix,
                float[] values) { return this; }
        public Transaction setWindowCrop(SurfaceControl surface, Rect crop) { return this; }
        public Transaction setCornerRadius(SurfaceControl surface, float radius) {
            return this;
        }
        public Transaction setLayer(SurfaceControl surface, int layer) { return this; }
        public Transaction setAnimationTransaction() { return this; }
        public void apply() {}
        @Override public void close() {}
    }
}
