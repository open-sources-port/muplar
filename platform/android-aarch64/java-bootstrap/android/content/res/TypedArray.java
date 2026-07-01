package android.content.res;

import android.util.AttributeSet;
import android.util.DisplayMetrics;
import android.util.TypedValue;

public final class TypedArray implements AutoCloseable {
    private final AttributeSet attributes;
    private final int[] styleable;
    private final int[] positions;
    private final DisplayMetrics metrics;
    private final int[] arrayData;
    private final int[] arrayTypes;
    public TypedArray(AttributeSet attributes, int[] styleable, DisplayMetrics metrics) {
        this.attributes = attributes;
        this.styleable = styleable == null ? new int[0] : styleable.clone();
        this.metrics = metrics;
        arrayData = null;
        arrayTypes = null;
        positions = new int[this.styleable.length];
        for (int i = 0; i < positions.length; i++) {
            positions[i] = -1;
            if (attributes == null) continue;
            for (int j = 0; j < attributes.getAttributeCount(); j++)
                if (attributes.getAttributeNameResource(j) == this.styleable[i]) {
                    positions[i] = j;
                    break;
                }
        }
    }
    TypedArray(int[] data, int[] types, DisplayMetrics metrics) {
        this(data, types, null, metrics);
    }
    TypedArray(int[] data, int[] types, boolean[] present, DisplayMetrics metrics) {
        attributes = null;
        styleable = new int[data.length];
        positions = new int[data.length];
        arrayData = data;
        arrayTypes = types;
        this.metrics = metrics;
        for (int i = 0; i < positions.length; i++)
            positions[i] = present == null || present[i] ? i : -1;
    }
    public int length() { return styleable.length; }
    public int getIndexCount() {
        int count = 0; for (int position : positions) if (position >= 0) count++;
        return count;
    }
    public int getIndex(int at) {
        for (int i = 0, found = 0; i < positions.length; i++)
            if (positions[i] >= 0 && found++ == at) return i;
        throw new ArrayIndexOutOfBoundsException(at);
    }
    public boolean hasValue(int index) { return position(index) >= 0; }
    public String getString(int index) {
        if (arrayData != null)
            return position(index) >= 0
                ? Integer.toString(arrayData[index]) : null;
        int position = position(index);
        return position < 0 ? null : attributes.getAttributeValue(position);
    }
    public CharSequence getText(int index) { return getString(index); }
    public int getInt(int index, int fallback) {
        if (arrayData != null)
            return position(index) >= 0 ? arrayData[index] : fallback;
        int position = position(index);
        return position < 0 ? fallback
            : attributes.getAttributeIntValue(position, parseInt(getString(index), fallback));
    }
    public int getInteger(int index, int fallback) { return getInt(index, fallback); }
    public int getColor(int index, int fallback) { return getInt(index, fallback); }
    public boolean getBoolean(int index, boolean fallback) {
        if (arrayData != null)
            return position(index) >= 0 ? arrayData[index] != 0 : fallback;
        int position = position(index);
        return position < 0 ? fallback
            : attributes.getAttributeBooleanValue(position, fallback);
    }
    public float getFloat(int index, float fallback) {
        if (arrayData != null) {
            if (position(index) < 0) return fallback;
            return arrayTypes[index] == 4 ? Float.intBitsToFloat(arrayData[index])
                : arrayData[index];
        }
        int position = position(index);
        return position < 0 ? fallback
            : attributes.getAttributeFloatValue(position, fallback);
    }
    public int getResourceId(int index, int fallback) {
        if (arrayData != null)
            return position(index) >= 0 && arrayTypes[index] == 1
                ? arrayData[index] : fallback;
        int position = position(index);
        return position < 0 ? fallback
            : attributes.getAttributeResourceValue(position, fallback);
    }
    public float getDimension(int index, float fallback) {
        if (arrayData != null) {
            if (position(index) < 0) return fallback;
            return arrayTypes[index] == 5
                ? TypedValue.complexToDimension(arrayData[index], metrics)
                : arrayData[index];
        }
        int position = position(index);
        if (position < 0) return fallback;
        return TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP,
            getFloat(index, fallback), metrics);
    }
    public int getDimensionPixelSize(int index, int fallback) {
        return Math.round(getDimension(index, fallback));
    }
    public int getDimensionPixelOffset(int index, int fallback) {
        return (int)getDimension(index, fallback);
    }
    public int getType(int index) {
        if (arrayTypes != null)
            return position(index) < 0 ? 0 : arrayTypes[index];
        int position = position(index);
        return position < 0 ? 0 : attributes.getAttributeValueType(position);
    }
    public TypedValue peekValue(int index) {
        if (!hasValue(index)) return null;
        TypedValue value = new TypedValue();
        value.type = getType(index);
        value.data = arrayData != null ? arrayData[index]
            : attributes.getAttributeValueData(position(index));
        value.resourceId = value.type == 1 ? value.data : 0;
        return value;
    }
    public void recycle() {}
    @Override public void close() { recycle(); }
    private int position(int index) {
        return index < 0 || index >= positions.length ? -1 : positions[index];
    }
    private static int parseInt(String value, int fallback) {
        try { return Integer.decode(value); }
        catch (Exception ignored) { return fallback; }
    }
}
