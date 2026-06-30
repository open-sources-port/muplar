package android.content;

import android.content.pm.PackageManager;
import android.content.res.Resources;

public abstract class Context {
    private static final PackageManager pmInstance = new PackageManager();
    private static final Resources resourcesInstance = new Resources();
    private final Resources.Theme theme = resourcesInstance.newTheme();

    public PackageManager getPackageManager() {
        return pmInstance;
    }

    public Resources getResources() {
        return resourcesInstance;
    }

    public String getString(int id) {
        return resourcesInstance.getString(id);
    }

    public Resources.Theme getTheme() { return theme; }
}
