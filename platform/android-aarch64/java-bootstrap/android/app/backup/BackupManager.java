package android.app.backup;

import android.content.Context;

public class BackupManager {
    private final BackupRestoreEventLogger restoreLogger =
        new BackupRestoreEventLogger();
    public BackupManager(Context context) {}
    public void dataChanged() {}
    public static void dataChanged(String packageName) {}
    public BackupRestoreEventLogger getDelayedRestoreLogger() { return restoreLogger; }
    public void reportDelayedRestoreResult(BackupRestoreEventLogger logger) {}
}
