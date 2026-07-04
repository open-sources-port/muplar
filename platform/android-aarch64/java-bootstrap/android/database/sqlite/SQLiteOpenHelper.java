package android.database.sqlite;

import android.content.Context;

public abstract class SQLiteOpenHelper implements AutoCloseable {
    private SQLiteDatabase database;
    private final Context context;
    private final String databaseName;
    private final int databaseVersion;
    public SQLiteOpenHelper(Context context, String name,
            SQLiteDatabase.CursorFactory factory, int version) {
        this.context = context;
        databaseName = name;
        databaseVersion = version;
    }
    public SQLiteOpenHelper(Context context, String name, int version,
            SQLiteDatabase.OpenParams openParams) {
        this.context = context;
        databaseName = name;
        databaseVersion = version;
    }
    public String getDatabaseName() { return databaseName; }
    public synchronized SQLiteDatabase getWritableDatabase() {
        if (database == null || !database.isOpen()) {
            database = new SQLiteDatabase(context == null || databaseName == null
                ? null : context.getDatabasePath(databaseName));
            onConfigure(database);
            int oldVersion = database.getVersion();
            if (oldVersion == 0) onCreate(database);
            else if (oldVersion < databaseVersion)
                onUpgrade(database, oldVersion, databaseVersion);
            else if (oldVersion > databaseVersion)
                onDowngrade(database, oldVersion, databaseVersion);
            if (oldVersion != databaseVersion) database.setVersion(databaseVersion);
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
