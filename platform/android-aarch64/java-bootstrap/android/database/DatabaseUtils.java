package android.database;

import android.database.sqlite.SQLiteStatement;

public final class DatabaseUtils {
    private DatabaseUtils() {}

    public static long longForQuery(SQLiteStatement statement, String[] selectionArgs) {
        if (statement == null) return 0;
        if (selectionArgs != null) {
            for (int index = 0; index < selectionArgs.length; index++) {
                statement.bindString(index + 1, selectionArgs[index]);
            }
        }
        return statement.simpleQueryForLong();
    }
}
