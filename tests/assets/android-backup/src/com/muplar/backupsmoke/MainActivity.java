package com.muplar.backupsmoke;

import android.app.Activity;
import android.app.backup.BackupManager;
import android.os.Bundle;
import java.lang.reflect.Method;

public final class MainActivity extends Activity {
    @Override
    public void onCreate(Bundle saved) {
        super.onCreate(saved);
        try {
            Class<?> sm = Class.forName("android.os.ServiceManager");
            for (String name : new String[] {"getService", "checkService"}) {
                Method lookup = sm.getMethod(name, String.class);
                for (int i = 0; i < 2; i++) {
                    if (lookup.invoke(null, "backup") != null) {
                        throw new AssertionError(name + " fabricated a backup service");
                    }
                }
            }
            // Framework callers must handle unavailable backup without creating
            // a remote AIDL proxy or claiming that backup was performed.
            BackupManager manager = new BackupManager(this);
            manager.dataChanged();
            BackupManager.dataChanged(getPackageName());
            if (Boolean.TRUE.equals(BackupManager.class.getMethod("isBackupEnabled").invoke(manager))) {
                throw new AssertionError("Backup reported enabled without a service");
            }
            System.out.println("BACKUP_UNAVAILABLE_SMOKE_PASS service=absent enabled=false");
        } catch (ReflectiveOperationException e) {
            throw new AssertionError(e);
        }
    }
}
