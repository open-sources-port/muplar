package android.appwidget;

import android.content.ComponentName;
import android.content.Context;
import android.content.pm.PackageManager;
import android.graphics.drawable.Drawable;
import android.os.UserHandle;

public class AppWidgetProviderInfo {
    public ComponentName provider;
    public int minWidth;
    public int minHeight;
    public int minResizeWidth;
    public int minResizeHeight;
    public int maxResizeWidth;
    public int maxResizeHeight;
    public int targetCellWidth;
    public int targetCellHeight;
    public int initialLayout;
    public int initialKeyguardLayout;
    public int previewImage;
    public int previewLayout;
    public int icon;
    public int resizeMode;
    public int widgetCategory;
    public int widgetFeatures;
    public ComponentName configure;
    public String label;
    UserHandle profile = UserHandle.CURRENT;
    public UserHandle getProfile() { return profile; }
    public CharSequence loadLabel(PackageManager packageManager) {
        return label == null ? provider.getPackageName() : label;
    }
    public Drawable loadIcon(Context context, int density) {
        try { return context.getPackageManager().getApplicationIcon(
            provider.getPackageName()); }
        catch (PackageManager.NameNotFoundException ignored) { return null; }
    }
    public Drawable loadPreviewImage(Context context, int density) { return null; }
}
