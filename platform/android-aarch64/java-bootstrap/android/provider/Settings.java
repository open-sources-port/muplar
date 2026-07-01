package android.provider;

import android.content.ContentResolver;
import android.net.Uri;
import java.util.concurrent.ConcurrentHashMap;
import com.muplar.runtime.FrameworkServiceClient;

public final class Settings {
    private Settings() {}
    private static final ConcurrentHashMap<String, String> VALUES =
        new ConcurrentHashMap<String, String>();

    private static String get(String namespace, String name) {
        String key = "settings:" + namespace + ":" + name;
        String remote = FrameworkServiceClient.request("settings-get", key);
        return remote == null ? VALUES.get(key) : remote;
    }
    private static boolean put(String namespace, String name, String value) {
        String key = "settings:" + namespace + ":" + name;
        String actual = value == null ? "" : value;
        String remote = FrameworkServiceClient.request("settings-put",
            key + "\n" + actual);
        if (remote != null) return "1".equals(remote);
        if (value == null) VALUES.remove(key);
        else VALUES.put(key, value);
        return true;
    }
    private static int getInt(String namespace, String name, int defaultValue) {
        String value = get(namespace, name);
        if (value == null) return defaultValue;
        try { return Integer.parseInt(value); }
        catch (NumberFormatException ignored) { return defaultValue; }
    }

    public static final class System {
        public static final Uri CONTENT_URI = Uri.parse("content://settings/system");
        public static Uri getUriFor(String name) {
            return Uri.parse("content://settings/system/" + name);
        }
        public static String getString(ContentResolver resolver, String name) {
            return get("system", name);
        }
        public static boolean putString(ContentResolver resolver, String name,
                                        String value) {
            return put("system", name, value);
        }
        public static int getInt(ContentResolver resolver, String name,
                int defaultValue) { return Settings.getInt("system", name, defaultValue); }
        public static boolean putInt(ContentResolver resolver, String name, int value) {
            return put("system", name, Integer.toString(value));
        }
    }
    public static final class Secure {
        public static final Uri CONTENT_URI = Uri.parse("content://settings/secure");
        public static Uri getUriFor(String name) {
            return Uri.parse("content://settings/secure/" + name);
        }
        public static String getString(ContentResolver resolver, String name) {
            return get("secure", name);
        }
        public static boolean putString(ContentResolver resolver, String name,
                                        String value) {
            return put("secure", name, value);
        }
        public static int getInt(ContentResolver resolver, String name,
                int defaultValue) { return Settings.getInt("secure", name, defaultValue); }
        public static boolean putInt(ContentResolver resolver, String name, int value) {
            return put("secure", name, Integer.toString(value));
        }
    }
    public static final class Global {
        public static final Uri CONTENT_URI = Uri.parse("content://settings/global");
        public static Uri getUriFor(String name) {
            return Uri.parse("content://settings/global/" + name);
        }
        public static String getString(ContentResolver resolver, String name) {
            return get("global", name);
        }
        public static boolean putString(ContentResolver resolver, String name,
                                        String value) {
            return put("global", name, value);
        }
        public static int getInt(ContentResolver resolver, String name,
                int defaultValue) { return Settings.getInt("global", name, defaultValue); }
        public static boolean putInt(ContentResolver resolver, String name, int value) {
            return put("global", name, Integer.toString(value));
        }
    }
}
