package android.content.res;

import java.util.Locale;

public class Configuration {
    public static final int ORIENTATION_UNDEFINED = 0;
    public static final int ORIENTATION_PORTRAIT = 1;
    public static final int ORIENTATION_LANDSCAPE = 2;
    public int orientation = ORIENTATION_PORTRAIT;
    public int sdkVersion = 35;
    public Locale locale = Locale.getDefault();
}
