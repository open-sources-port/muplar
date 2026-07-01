package android.content.res;

import java.util.Locale;
import android.app.WindowConfiguration;
import android.os.LocaleList;

public class Configuration {
    public static final int ORIENTATION_UNDEFINED = 0;
    public static final int ORIENTATION_PORTRAIT = 1;
    public static final int ORIENTATION_LANDSCAPE = 2;
    public int orientation = ORIENTATION_PORTRAIT;
    public int sdkVersion = 35;
    public Locale locale = Locale.getDefault();
    public int densityDpi = 160;
    public int screenWidthDp = 420;
    public int screenHeightDp = 280;
    public int smallestScreenWidthDp = 280;
    public int uiMode;
    public float fontScale = 1.0f;
    public final WindowConfiguration windowConfiguration =
        new WindowConfiguration();
    public Configuration() {}
    public Configuration(Configuration source) { setTo(source); }
    public void setTo(Configuration source) {
        orientation = source.orientation;
        sdkVersion = source.sdkVersion;
        locale = source.locale;
        densityDpi = source.densityDpi;
        screenWidthDp = source.screenWidthDp;
        screenHeightDp = source.screenHeightDp;
        smallestScreenWidthDp = source.smallestScreenWidthDp;
        uiMode = source.uiMode;
        fontScale = source.fontScale;
        windowConfiguration.setTo(source.windowConfiguration);
    }
    public LocaleList getLocales() { return new LocaleList(locale); }
    public void setLocales(LocaleList locales) {
        if (locales != null && !locales.isEmpty()) locale = locales.get(0);
    }
}
