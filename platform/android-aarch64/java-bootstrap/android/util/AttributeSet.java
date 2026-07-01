package android.util;

public interface AttributeSet {
    int getAttributeCount();
    String getAttributeName(int index);
    String getAttributeValue(int index);
    String getAttributeValue(String namespace, String name);
    int getAttributeNameResource(int index);
    default int getAttributeValueType(int index) { return 0; }
    default int getAttributeValueData(int index) { return 0; }
    default String getPositionDescription() { return ""; }
    default int getAttributeResourceValue(int index, int fallback) { return fallback; }
    default int getAttributeIntValue(int index, int fallback) { return fallback; }
    default float getAttributeFloatValue(int index, float fallback) { return fallback; }
    default boolean getAttributeBooleanValue(int index, boolean fallback) {
        String value = getAttributeValue(index);
        return value == null ? fallback : Boolean.parseBoolean(value);
    }
    default int getStyleAttribute() { return 0; }
}
