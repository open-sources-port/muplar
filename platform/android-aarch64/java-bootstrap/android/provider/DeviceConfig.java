package android.provider;

import java.util.Collections;
import java.util.HashMap;
import java.util.Map;
import java.util.Set;

public final class DeviceConfig {
    private DeviceConfig() {
    }

    public static Properties getProperties(String namespace, String... names) {
        return new Properties(namespace, Collections.<String, String>emptyMap());
    }

    public static boolean getBoolean(String namespace,
                                     String name,
                                     boolean defaultValue) {
        return defaultValue;
    }

    public static final class Properties {
        private final String namespace;
        private final Map<String, String> values;

        public Properties(String namespace, Map<String, String> values) {
            this.namespace = namespace;
            this.values = values != null
                ? new HashMap<String, String>(values)
                : new HashMap<String, String>();
        }

        public String getNamespace() {
            return namespace;
        }

        public Set<String> getKeyset() {
            return Collections.unmodifiableSet(values.keySet());
        }

        public String getString(String name, String defaultValue) {
            String value = values.get(name);
            return value != null ? value : defaultValue;
        }

        public boolean getBoolean(String name, boolean defaultValue) {
            String value = values.get(name);
            return value != null ? Boolean.parseBoolean(value) : defaultValue;
        }

        public int getInt(String name, int defaultValue) {
            String value = values.get(name);
            if (value == null) {
                return defaultValue;
            }
            try {
                return Integer.parseInt(value);
            } catch (NumberFormatException ignored) {
                return defaultValue;
            }
        }

        public long getLong(String name, long defaultValue) {
            String value = values.get(name);
            if (value == null) {
                return defaultValue;
            }
            try {
                return Long.parseLong(value);
            } catch (NumberFormatException ignored) {
                return defaultValue;
            }
        }

        public float getFloat(String name, float defaultValue) {
            String value = values.get(name);
            if (value == null) {
                return defaultValue;
            }
            try {
                return Float.parseFloat(value);
            } catch (NumberFormatException ignored) {
                return defaultValue;
            }
        }
    }
}
