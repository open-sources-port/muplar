package android.os;

import com.muplar.runtime.FrameworkServiceClient;

public final class SystemProperties {
    private SystemProperties() {}
    public static String get(String key) { return get(key, ""); }
    public static String get(String key, String defaultValue) {
        String remote = FrameworkServiceClient.request("settings-get",
            "property:" + key);
        if (remote != null && !remote.isEmpty()) return remote;
        return java.lang.System.getProperty("muplar.android.property." + key,
            defaultValue);
    }
    public static int getInt(String key, int defaultValue) {
        try { return Integer.parseInt(get(key)); }
        catch (NumberFormatException error) { return defaultValue; }
    }
    public static boolean getBoolean(String key, boolean defaultValue) {
        String value = get(key);
        if (value.isEmpty()) return defaultValue;
        return "1".equals(value) || "true".equalsIgnoreCase(value) ||
            "yes".equalsIgnoreCase(value);
    }
    public static void set(String key, String value) {
        String remote = FrameworkServiceClient.request("settings-put",
            "property:" + key + "\n" + value);
        if ("1".equals(remote)) return;
        java.lang.System.setProperty("muplar.android.property." + key, value);
    }
}
