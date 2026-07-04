package android.app.backup;

public class BackupRestoreEventLogger {
    public void logItemsRestored(String dataType, int count) {}
    public void logItemsRestoreFailed(String dataType, int count, String error) {}
}
