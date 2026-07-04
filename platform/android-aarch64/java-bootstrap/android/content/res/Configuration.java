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
    public int getLayoutDirection() {
        String language = locale == null ? "" : locale.getLanguage();
        return "ar".equals(language) || "fa".equals(language) ||
            "he".equals(language) || "ur".equals(language) ? 1 : 0;
    }
    public boolean equals(Configuration other) { return equals((Object) other); }
    @Override public boolean equals(Object value) {
        if (!(value instanceof Configuration)) return false;
        Configuration other = (Configuration) value;
        return orientation == other.orientation && sdkVersion == other.sdkVersion &&
            densityDpi == other.densityDpi && screenWidthDp == other.screenWidthDp &&
            screenHeightDp == other.screenHeightDp &&
            smallestScreenWidthDp == other.smallestScreenWidthDp &&
            uiMode == other.uiMode && Float.compare(fontScale, other.fontScale) == 0 &&
            java.util.Objects.equals(locale, other.locale);
    }
    @Override public int hashCode() {
        return java.util.Objects.hash(orientation, sdkVersion, locale, densityDpi,
            screenWidthDp, screenHeightDp, smallestScreenWidthDp, uiMode, fontScale);
    }
}
