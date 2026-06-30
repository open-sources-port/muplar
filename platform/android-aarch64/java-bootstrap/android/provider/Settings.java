package android.provider;

import android.content.ContentResolver;
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

    public static final class System {
        public static String getString(ContentResolver resolver, String name) {
            return get("system", name);
        }
        public static boolean putString(ContentResolver resolver, String name,
                                        String value) {
            return put("system", name, value);
        }
    }
    public static final class Secure {
        public static String getString(ContentResolver resolver, String name) {
            return get("secure", name);
        }
        public static boolean putString(ContentResolver resolver, String name,
                                        String value) {
            return put("secure", name, value);
        }
    }
    public static final class Global {
        public static String getString(ContentResolver resolver, String name) {
            return get("global", name);
        }
        public static boolean putString(ContentResolver resolver, String name,
                                        String value) {
            return put("global", name, value);
        }
    }
}
