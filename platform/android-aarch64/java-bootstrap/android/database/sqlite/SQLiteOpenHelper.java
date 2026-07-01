package android.database.sqlite;

import android.content.Context;

public abstract class SQLiteOpenHelper implements AutoCloseable {
    private SQLiteDatabase database;
    public SQLiteOpenHelper(Context context, String name,
            SQLiteDatabase.CursorFactory factory, int version) {}
    public SQLiteOpenHelper(Context context, String name, int version,
            SQLiteDatabase.OpenParams openParams) {}
    public synchronized SQLiteDatabase getWritableDatabase() {
        if (database == null || !database.isOpen()) {
            database = new SQLiteDatabase();
            onConfigure(database);
            onCreate(database);
            onOpen(database);
        }
        return database;
    }
    public synchronized SQLiteDatabase getReadableDatabase() {
        return getWritableDatabase();
    }
    public void setOpenParams(SQLiteDatabase.OpenParams openParams) {}
    public void setWriteAheadLoggingEnabled(boolean enabled) {}
    public synchronized void close() {
        if (database != null) database.close();
        database = null;
    }
    public void onConfigure(SQLiteDatabase database) {}
    public abstract void onCreate(SQLiteDatabase database);
    public abstract void onUpgrade(SQLiteDatabase database, int oldVersion, int newVersion);
    public void onDowngrade(SQLiteDatabase database, int oldVersion, int newVersion) {
        onUpgrade(database, oldVersion, newVersion);
    }
    public void onOpen(SQLiteDatabase database) {}
}
