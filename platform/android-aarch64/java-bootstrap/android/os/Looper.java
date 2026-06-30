package android.os;

import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ThreadFactory;

public final class Looper {
    private static final ThreadLocal<Looper> CURRENT = new ThreadLocal<Looper>();
    private static final Looper MAIN = new Looper("main", Thread.currentThread());
    final ScheduledExecutorService executor;
    private final Thread owner;

    Looper(final String name, Thread owner) {
        this.owner = owner;
        executor = Executors.newSingleThreadScheduledExecutor(new ThreadFactory() {
            @Override public Thread newThread(final Runnable action) {
                Thread thread = new Thread(new Runnable() {
                    @Override public void run() {
                        CURRENT.set(Looper.this);
                        action.run();
                    }
                }, "MuplarLooper-" + name);
                thread.setDaemon(true);
                return thread;
            }
        });
    }
    public static Looper getMainLooper() { return MAIN; }
    public static Looper myLooper() {
        Looper current = CURRENT.get();
        return current == null && Thread.currentThread() == MAIN.owner
            ? MAIN : current;
    }
    public Thread getThread() { return owner; }
    public boolean isCurrentThread() { return myLooper() == this; }
    public void quit() { executor.shutdownNow(); }
    public void quitSafely() { executor.shutdown(); }
}
