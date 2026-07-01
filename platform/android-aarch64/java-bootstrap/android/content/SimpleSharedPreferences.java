package android.content;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Properties;
import java.util.Set;
import java.util.concurrent.CopyOnWriteArraySet;

final class SimpleSharedPreferences implements SharedPreferences {
    private final File file;
    private final Map<String, String> values = new HashMap<>();
    private final Set<OnSharedPreferenceChangeListener> listeners =
        new CopyOnWriteArraySet<>();
    SimpleSharedPreferences(File file) { this.file = file; load(); }
    private synchronized void load() {
        if (!file.isFile()) return;
        Properties properties = new Properties();
        try (FileInputStream input = new FileInputStream(file)) {
            properties.load(input);
            for (String key : properties.stringPropertyNames())
                values.put(key, properties.getProperty(key));
        } catch (Exception ignored) {}
    }
    public synchronized Map<String, ?> getAll() {
        return Collections.unmodifiableMap(new HashMap<>(values));
    }
    public synchronized String getString(String key, String fallback) {
        String value = values.get(key); return value == null ? fallback : value;
    }
    public synchronized Set<String> getStringSet(String key, Set<String> fallback) {
        String value = values.get(key);
        if (value == null) return fallback;
        Set<String> result = new HashSet<>();
        if (!value.isEmpty()) Collections.addAll(result, value.split("\\u001f", -1));
        return result;
    }
    public int getInt(String key, int fallback) {
        try { return Integer.parseInt(getString(key, null)); }
        catch (Exception ignored) { return fallback; }
    }
    public long getLong(String key, long fallback) {
        try { return Long.parseLong(getString(key, null)); }
        catch (Exception ignored) { return fallback; }
    }
    public float getFloat(String key, float fallback) {
        try { return Float.parseFloat(getString(key, null)); }
        catch (Exception ignored) { return fallback; }
    }
    public boolean getBoolean(String key, boolean fallback) {
        String value = getString(key, null);
        return value == null ? fallback : Boolean.parseBoolean(value);
    }
    public synchronized boolean contains(String key) { return values.containsKey(key); }
    public Editor edit() { return new EditorImpl(); }
    public void registerOnSharedPreferenceChangeListener(
            OnSharedPreferenceChangeListener listener) {
        if (listener != null) listeners.add(listener);
    }
    public void unregisterOnSharedPreferenceChangeListener(
            OnSharedPreferenceChangeListener listener) { listeners.remove(listener); }
    private final class EditorImpl implements Editor {
        private final Map<String, String> updates = new HashMap<>();
        private final Set<String> removals = new HashSet<>();
        private boolean clear;
        public Editor putString(String k, String v) { updates.put(k, v); return this; }
        public Editor putStringSet(String k, Set<String> v) {
            updates.put(k, v == null ? null : String.join("\u001f", v)); return this;
        }
        public Editor putInt(String k, int v) { return putString(k, Integer.toString(v)); }
        public Editor putLong(String k, long v) { return putString(k, Long.toString(v)); }
        public Editor putFloat(String k, float v) { return putString(k, Float.toString(v)); }
        public Editor putBoolean(String k, boolean v) { return putString(k, Boolean.toString(v)); }
        public Editor remove(String k) { removals.add(k); return this; }
        public Editor clear() { clear = true; return this; }
        public boolean commit() { return applyChanges(); }
        public void apply() { applyChanges(); }
        private boolean applyChanges() {
            Set<String> changed = new HashSet<>();
            synchronized (SimpleSharedPreferences.this) {
                if (clear) { changed.addAll(values.keySet()); values.clear(); }
                for (String key : removals) if (values.remove(key) != null) changed.add(key);
                for (Map.Entry<String, String> update : updates.entrySet()) {
                    if (update.getValue() == null) values.remove(update.getKey());
                    else values.put(update.getKey(), update.getValue());
                    changed.add(update.getKey());
                }
                try {
                    File parent = file.getParentFile();
                    if (parent != null) parent.mkdirs();
                    Properties properties = new Properties();
                    properties.putAll(values);
                    try (FileOutputStream output = new FileOutputStream(file)) {
                        properties.store(output, "Muplar Android preferences");
                    }
                } catch (Exception error) { return false; }
            }
            for (String key : changed)
                for (OnSharedPreferenceChangeListener listener : listeners)
                    listener.onSharedPreferenceChanged(SimpleSharedPreferences.this, key);
            return true;
        }
    }
}
