package android.content.pm;

import android.graphics.drawable.Drawable;

public abstract class MuplarPackageManagerBridge extends PackageManager {
    public Drawable loadItemIcon(PackageItemInfo itemInfo, ApplicationInfo appInfo) {
        return muplarLoadItemIcon(itemInfo, appInfo);
    }

    public Drawable loadItemIcon(PackageItemInfo itemInfo, ApplicationInfo appInfo,
                          boolean isArchived) {
        return muplarLoadItemIcon(itemInfo, appInfo);
    }

    public Drawable loadItemIcon(PackageItemInfo itemInfo, ApplicationInfo appInfo,
                          boolean isArchived, int flags) {
        return muplarLoadItemIcon(itemInfo, appInfo);
    }

    public Drawable loadUnbadgedItemIcon(PackageItemInfo itemInfo,
                                  ApplicationInfo appInfo) {
        return muplarLoadItemIcon(itemInfo, appInfo);
    }

    protected abstract Drawable muplarLoadItemIcon(PackageItemInfo itemInfo,
                                                   ApplicationInfo appInfo);
}
