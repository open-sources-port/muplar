package android.database;

public interface Cursor extends java.io.Closeable {
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
    android.os.Bundle getExtras();
    void setExtras(android.os.Bundle extras);
    void close();
}
