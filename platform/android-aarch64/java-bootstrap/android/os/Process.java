package android.os;

public final class Process {
    public static final int THREAD_PRIORITY_DEFAULT = 0;
    public static final int THREAD_PRIORITY_FOREGROUND = -2;
    public static final int THREAD_PRIORITY_BACKGROUND = 10;
    private Process() {}
    public static int myPid() { return 1; }
    public static int myUid() { return 10000; }
    public static int myTid() { return (int)Thread.currentThread().getId(); }
    public static void setThreadPriority(int priority) {}
    public static void setThreadPriority(int threadId, int priority) {}
    public static int getThreadPriority(int threadId) { return 0; }
}
