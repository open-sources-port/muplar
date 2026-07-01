package android.database;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

public class MatrixCursor implements Cursor {
    private final String[] columns;
    private final List<Object[]> rows = new ArrayList<>();
    private int position = -1;
    public MatrixCursor(String[] columns) { this.columns = columns.clone(); }
    public void addRow(Object[] values) { rows.add(values.clone()); }
    public int getCount() { return rows.size(); }
    public boolean moveToFirst() { position = rows.isEmpty() ? -1 : 0; return position == 0; }
    public boolean moveToNext() { if (position + 1 >= rows.size()) return false; position++; return true; }
    public int getColumnIndex(String name) {
        for (int i = 0; i < columns.length; i++) if (columns[i].equals(name)) return i;
        return -1;
    }
    public int getColumnIndexOrThrow(String name) {
        int index = getColumnIndex(name);
        if (index < 0) throw new IllegalArgumentException("column not found: " + name);
        return index;
    }
    public String getString(int index) { Object value = value(index); return value == null ? null : value.toString(); }
    public int getInt(int index) { Object value = value(index); return value instanceof Number ? ((Number)value).intValue() : 0; }
    public long getLong(int index) { Object value = value(index); return value instanceof Number ? ((Number)value).longValue() : 0; }
    public byte[] getBlob(int index) { Object value = value(index); return value instanceof byte[] ? (byte[])value : null; }
    public boolean isNull(int index) { return value(index) == null; }
    public void close() {}
    private Object value(int index) {
        if (position < 0 || position >= rows.size()) throw new IllegalStateException("cursor not positioned");
        return rows.get(position)[index];
    }
}
