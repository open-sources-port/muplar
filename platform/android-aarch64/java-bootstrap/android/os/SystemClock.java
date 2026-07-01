package android.os;

public final class SystemClock {
    private SystemClock() {}
    public static long uptimeMillis() { return System.nanoTime() / 1_000_000L; }
    public static long elapsedRealtime() { return uptimeMillis(); }
    public static long elapsedRealtimeNanos() { return System.nanoTime(); }
    public static void sleep(long milliseconds) {
        try { Thread.sleep(Math.max(0, milliseconds)); }
        catch (InterruptedException error) { Thread.currentThread().interrupt(); }
    }
}
