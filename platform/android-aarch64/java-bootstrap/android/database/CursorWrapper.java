package android.database;

public class CursorWrapper implements Cursor {
    protected final Cursor cursor;
    public CursorWrapper(Cursor cursor) { this.cursor = cursor; }
    public Cursor getWrappedCursor() { return cursor; }
    public int getCount() { return cursor.getCount(); }
    public boolean moveToFirst() { return cursor.moveToFirst(); }
    public boolean moveToNext() { return cursor.moveToNext(); }
    public int getColumnIndex(String name) { return cursor.getColumnIndex(name); }
    public int getColumnIndexOrThrow(String name) {
        return cursor.getColumnIndexOrThrow(name);
    }
    public String getString(int columnIndex) { return cursor.getString(columnIndex); }
    public int getInt(int columnIndex) { return cursor.getInt(columnIndex); }
    public long getLong(int columnIndex) { return cursor.getLong(columnIndex); }
    public byte[] getBlob(int columnIndex) { return cursor.getBlob(columnIndex); }
    public boolean isNull(int columnIndex) { return cursor.isNull(columnIndex); }
    public android.os.Bundle getExtras() { return cursor.getExtras(); }
    public void setExtras(android.os.Bundle extras) { cursor.setExtras(extras); }
    public void close() { cursor.close(); }
}
