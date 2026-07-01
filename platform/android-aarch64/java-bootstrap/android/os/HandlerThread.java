package android.os;

public class HandlerThread extends Thread {
    private final int priority;
    private Looper looper;
    public HandlerThread(String name) { this(name, Process.THREAD_PRIORITY_DEFAULT); }
    public HandlerThread(String name, int priority) {
        super(name); this.priority = priority; setDaemon(true);
    }
    @Override public synchronized void start() {
        if (looper == null) looper = new Looper(getName(), this);
        super.start();
    }
    @Override public void run() {
        Process.setThreadPriority(priority);
        try { while (looper != null && !isInterrupted()) Thread.sleep(1000); }
        catch (InterruptedException ignored) { interrupt(); }
    }
    public Looper getLooper() {
        if (looper == null) looper = new Looper(getName(), this);
        return looper;
    }
    public boolean quit() { getLooper().quit(); interrupt(); return true; }
    public boolean quitSafely() { getLooper().quitSafely(); interrupt(); return true; }
}
