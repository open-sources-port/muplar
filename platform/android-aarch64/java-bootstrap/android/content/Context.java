package android.content;

import android.content.pm.PackageManager;

public abstract class Context {
    private static final PackageManager pmInstance = new PackageManager();

    public PackageManager getPackageManager() {
        return pmInstance;
    }
}
