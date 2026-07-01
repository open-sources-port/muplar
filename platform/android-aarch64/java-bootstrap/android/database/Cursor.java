package android.database;

public interface Cursor extends AutoCloseable {
    int getCount();
    boolean moveToFirst();
    boolean moveToNext();
    int getColumnIndex(String name);
    int getColumnIndexOrThrow(String name);
    String getString(int columnIndex);
    int getInt(int columnIndex);
    long getLong(int columnIndex);
    byte[] getBlob(int columnIndex);
    boolean isNull(int columnIndex);
    void close();
}
