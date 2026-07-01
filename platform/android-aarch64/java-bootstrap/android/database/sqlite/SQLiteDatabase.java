package android.database.sqlite;

import android.content.ContentValues;
import android.database.Cursor;
import android.database.MatrixCursor;

public class SQLiteDatabase {
    public static final int OPEN_READWRITE = 0;
    public static final int OPEN_READONLY = 1;
    public static final int NO_LOCALIZED_COLLATORS = 16;
    public interface CursorFactory {}
    private boolean open = true;
    public void execSQL(String sql) {}
    public void execSQL(String sql, Object[] bindArgs) {}
    public long insert(String table, String nullColumnHack, ContentValues values) { return 1; }
    public long insertOrThrow(String table, String nullColumnHack, ContentValues values) { return 1; }
    public long replace(String table, String nullColumnHack, ContentValues values) { return 1; }
    public int update(String table, ContentValues values, String where, String[] args) { return 0; }
    public int delete(String table, String where, String[] args) { return 0; }
    public Cursor query(String table, String[] columns, String selection,
            String[] selectionArgs, String groupBy, String having, String orderBy) {
        return new MatrixCursor(columns == null ? new String[0] : columns);
    }
    public Cursor rawQuery(String sql, String[] selectionArgs) {
        return new MatrixCursor(new String[0]);
    }
    public void beginTransaction() {}
    public void setTransactionSuccessful() {}
    public void endTransaction() {}
    public boolean isOpen() { return open; }
    public void close() { open = false; }
    public static final class OpenParams {
        private final int flags;
        private OpenParams(int flags) { this.flags = flags; }
        public int getOpenFlags() { return flags; }
        public static final class Builder {
            private int flags;
            public Builder() {}
            public Builder(OpenParams source) { flags = source.flags; }
            public Builder setOpenFlags(int value) { flags = value; return this; }
            public Builder addOpenFlags(int value) { flags |= value; return this; }
            public Builder setJournalMode(String mode) { return this; }
            public Builder setSynchronousMode(String mode) { return this; }
            public OpenParams build() { return new OpenParams(flags); }
        }
    }
}
