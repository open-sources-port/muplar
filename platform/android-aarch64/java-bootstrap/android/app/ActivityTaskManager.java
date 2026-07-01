package android.app;

public final class ActivityTaskManager {
    private static final IActivityTaskManager SERVICE =
        new IActivityTaskManager() {
            public void registerTaskStackListener(ITaskStackListener listener) {}
            public void unregisterTaskStackListener(ITaskStackListener listener) {}
            public RootTaskInfo getRootTaskInfo(int windowingMode, int activityType) {
                return null;
            }
        };
    public static class RootTaskInfo extends ActivityManager.RunningTaskInfo {
        public int[] childTaskIds = new int[0];
        public String[] childTaskNames = new String[0];
        public int[] childTaskUserIds = new int[0];
        public boolean visible;
    }
    private ActivityTaskManager() {}
    public static IActivityTaskManager getService() { return SERVICE; }
}
