package android.os;

import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledThreadPoolExecutor;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.Future;
import java.util.concurrent.CancellationException;
import java.util.concurrent.ExecutionException;

public final class Looper {
    private static final ThreadLocal<Looper> CURRENT = new ThreadLocal<Looper>();
    private static final Looper MAIN = new Looper("main", Thread.currentThread());
    final ScheduledExecutorService executor;
    private final Thread owner;
    private final MessageQueue queue = new MessageQueue();

    Looper(final String name, Thread owner) {
        this.owner = owner;
        ThreadFactory factory = new ThreadFactory() {
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
        };
        executor = new ScheduledThreadPoolExecutor(1, factory) {
            @Override protected void afterExecute(Runnable action, Throwable error) {
                super.afterExecute(action, error);
                if (error == null && action instanceof Future<?>) {
                    try { ((Future<?>) action).get(); }
                    catch (CancellationException ignored) { return; }
                    catch (InterruptedException interrupted) {
                        Thread.currentThread().interrupt();
                        return;
                    } catch (ExecutionException failed) {
                        error = failed.getCause();
                    }
                }
                if (error != null) {
                    System.err.println("[Looper] task failed on " + name + ": " + error);
                    error.printStackTrace(System.err);
                }
            }
        };
    }
    public static Looper getMainLooper() { return MAIN; }
    public static Looper myLooper() {
        Looper current = CURRENT.get();
        return current == null && Thread.currentThread() == MAIN.owner
            ? MAIN : current;
    }
    public Thread getThread() { return owner; }
    public MessageQueue getQueue() { return queue; }
    public boolean isCurrentThread() { return myLooper() == this; }
    public void setTraceTag(long traceTag) {}
    public void quit() { executor.shutdownNow(); }
    public void quitSafely() { executor.shutdown(); }
}
