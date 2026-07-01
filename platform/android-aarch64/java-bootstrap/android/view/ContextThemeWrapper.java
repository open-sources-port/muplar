package android.view;

import android.content.Context;
import android.content.ContextWrapper;
import android.content.res.Resources;

public class ContextThemeWrapper extends ContextWrapper {
    private int themeResource;

    public ContextThemeWrapper() { super(null); }
    public ContextThemeWrapper(Context base, int themeResId) {
        super(base);
        themeResource = themeResId;
    }
    public ContextThemeWrapper(Context base, Resources.Theme theme) {
        super(base);
    }
    public void applyOverrideConfiguration(
            android.content.res.Configuration configuration) {}
    public int getThemeResId() { return themeResource; }
    public void setTheme(int resid) { themeResource = resid; }
    @Override public Resources getResources() {
        Context base = getBaseContext();
        return base == null ? super.getResources() : base.getResources();
    }
    @Override public Resources.Theme getTheme() {
        Context base = getBaseContext();
        return base == null ? super.getTheme() : base.getTheme();
    }
}
