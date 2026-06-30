package android.view;

import android.graphics.Rect;
import com.muplar.runtime.FrameworkServiceClient;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicLong;

public final class SurfaceControl implements AutoCloseable {
    private static final AtomicLong NEXT_ID = new AtomicLong(
        (System.currentTimeMillis() << 16) ^ ProcessHandleCompat.pid());
    private final long id;
    private boolean valid = true;

    private SurfaceControl(long id) { this.id = id; }
    public boolean isValid() { return valid; }
    public void release() { valid = false; }
    @Override public void close() { release(); }

    public static final class Builder {
        private String name = "Surface";
        private int width;
        private int height;
        public Builder() {}
        public Builder setName(String name) {
            if (name != null && !name.isEmpty()) this.name = sanitize(name);
            return this;
        }
        public Builder setBufferSize(int width, int height) {
            this.width = Math.max(0, width);
            this.height = Math.max(0, height);
            return this;
        }
        public Builder setContainerLayer() { return this; }
        public Builder setParent(SurfaceControl parent) { return this; }
        public SurfaceControl build() {
            long id = NEXT_ID.incrementAndGet();
            String result = FrameworkServiceClient.request("surface-transaction",
                "create\t" + id + "\t" + name + "\t" + width + "\t" + height);
            if (result == null || "0".equals(result))
                throw new IllegalStateException("surface service unavailable");
            return new SurfaceControl(id);
        }
    }

    public static final class Transaction implements AutoCloseable {
        private final List<String> commands = new ArrayList<String>();
        public Transaction show(SurfaceControl surface) {
            commands.add("show\t" + require(surface)); return this;
        }
        public Transaction hide(SurfaceControl surface) {
            commands.add("hide\t" + require(surface)); return this;
        }
        public Transaction setVisibility(SurfaceControl surface, boolean visible) {
            return visible ? show(surface) : hide(surface);
        }
        public Transaction setLayer(SurfaceControl surface, int layer) {
            commands.add("layer\t" + require(surface) + "\t" + layer); return this;
        }
        public Transaction setAlpha(SurfaceControl surface, float alpha) {
            commands.add("alpha\t" + require(surface) + "\t" +
                Math.max(0.0f, Math.min(1.0f, alpha))); return this;
        }
        public Transaction setPosition(SurfaceControl surface, float x, float y) {
            commands.add("position\t" + require(surface) + "\t" + x + "\t" + y);
            return this;
        }
        public Transaction setWindowCrop(SurfaceControl surface, int width, int height) {
            commands.add("crop\t" + require(surface) + "\t" +
                Math.max(0, width) + "\t" + Math.max(0, height));
            return this;
        }
        public Transaction setWindowCrop(SurfaceControl surface, Rect crop) {
            return setWindowCrop(surface,
                crop == null ? 0 : crop.width(), crop == null ? 0 : crop.height());
        }
        public Transaction remove(SurfaceControl surface) {
            commands.add("remove\t" + require(surface));
            surface.valid = false;
            return this;
        }
        public void apply() {
            if (commands.isEmpty()) return;
            StringBuilder payload = new StringBuilder();
            for (String command : commands) {
                if (payload.length() != 0) payload.append('\n');
                payload.append(command);
            }
            String result = FrameworkServiceClient.request(
                "surface-transaction", payload.toString());
            if (result == null || "0".equals(result))
                throw new IllegalStateException("surface transaction failed");
            commands.clear();
        }
        @Override public void close() { commands.clear(); }
        private static long require(SurfaceControl surface) {
            if (surface == null || !surface.valid)
                throw new IllegalArgumentException("invalid surface");
            return surface.id;
        }
    }

    private static String sanitize(String value) {
        return value.replace('\t', ' ').replace('\n', ' ');
    }

    private static final class ProcessHandleCompat {
        static long pid() {
            String runtime = java.lang.management.ManagementFactory
                .getRuntimeMXBean().getName();
            int separator = runtime.indexOf('@');
            try { return Long.parseLong(separator < 0 ? runtime :
                runtime.substring(0, separator)); }
            catch (NumberFormatException ignored) { return 1; }
        }
    }
}
