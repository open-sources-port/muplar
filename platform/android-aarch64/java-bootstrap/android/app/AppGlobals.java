package android.app;

import android.content.ComponentName;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.IPackageManager;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.os.Binder;
import android.os.IBinder;
import java.util.List;

public final class AppGlobals {
    private static final PackageManager PACKAGE_MANAGER = new PackageManager();
    private static final IPackageManager PACKAGE_SERVICE = new IPackageManager() {
        private final Binder binder = new Binder();
        public ActivityInfo getActivityInfo(ComponentName component, long flags,
                int userId) {
            try {
                return PACKAGE_MANAGER.getActivityInfo(component, (int) flags);
            } catch (PackageManager.NameNotFoundException ignored) {
                return null;
            }
        }
        public ComponentName getHomeActivities(List<ResolveInfo> activities) {
            if (activities != null)
                activities.addAll(PACKAGE_MANAGER.queryIntentActivities(
                    new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_HOME), 0));
            return null;
        }
        public ResolveInfo resolveIntent(Intent intent, String resolvedType,
                long flags, int userId) {
            return PACKAGE_MANAGER.resolveActivity(intent, (int) flags);
        }
        public IBinder asBinder() { return binder; }
    };
    private AppGlobals() {}
    public static IPackageManager getPackageManager() { return PACKAGE_SERVICE; }
    public static Application getInitialApplication() {
        Application application = Application.getCurrentApplication();
        return application == null ? new Application() : application;
    }
}
