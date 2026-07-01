package android.content;

import java.util.HashMap;
import java.util.Map;
import java.util.Set;

public final class ContentValues {
    private final Map<String, Object> values = new HashMap<>();
    public ContentValues() {}
    public ContentValues(ContentValues source) { values.putAll(source.values); }
    public void put(String key, String value) { values.put(key, value); }
    public void put(String key, Integer value) { values.put(key, value); }
    public void put(String key, Long value) { values.put(key, value); }
    public void put(String key, Boolean value) { values.put(key, value); }
    public void put(String key, byte[] value) { values.put(key, value); }
    public void putNull(String key) { values.put(key, null); }
    public Object get(String key) { return values.get(key); }
    public String getAsString(String key) {
        Object value = values.get(key); return value == null ? null : value.toString();
    }
    public Integer getAsInteger(String key) {
        Object value = values.get(key);
        return value instanceof Number ? ((Number)value).intValue() : null;
    }
    public Set<Map.Entry<String, Object>> valueSet() { return values.entrySet(); }
    public int size() { return values.size(); }
    public void clear() { values.clear(); }
}
