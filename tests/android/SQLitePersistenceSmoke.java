package android.database.sqlite;

import android.content.ContentValues;
import android.database.Cursor;
import java.io.File;

public final class SQLitePersistenceSmoke {
    public static void main(String[] args) {
        if (args.length != 1) throw new AssertionError("database path required");
        File file = new File(args[0]);

        SQLiteDatabase first = new SQLiteDatabase(file);
        first.execSQL("CREATE TABLE favorites " +
            "(_id INTEGER PRIMARY KEY, title TEXT, cellX INTEGER)");
        first.setVersion(3);
        ContentValues row = new ContentValues();
        row.put("_id", 7L);
        row.put("title", "Persisted App");
        row.put("cellX", 1);
        if (first.insert("favorites", null, row) != 7L)
            throw new AssertionError("insert id was not preserved");
        first.close();

        SQLiteDatabase second = new SQLiteDatabase(file);
        if (second.getVersion() != 3)
            throw new AssertionError("database version was not persisted");
        Cursor cursor = second.query("favorites",
            new String[]{"_id", "title", "cellX"}, "_id=?",
            new String[]{"7"}, null, null, null);
        if (!cursor.moveToFirst() || cursor.getLong(0) != 7L ||
                !"Persisted App".equals(cursor.getString(1)) || cursor.getInt(2) != 1)
            throw new AssertionError("persisted row did not round-trip");
        cursor.close();

        ContentValues update = new ContentValues();
        update.put("cellX", 4);
        if (second.update("favorites", update, "_id=?", new String[]{"7"}) != 1)
            throw new AssertionError("update did not match persisted row");
        if (second.delete("favorites", "_id=?", new String[]{"7"}) != 1)
            throw new AssertionError("delete did not match persisted row");
        second.close();

        if (!file.isFile() || file.length() == 0)
            throw new AssertionError("database file was not created");
        System.out.println("sqlitePersistence=ok path=" + file);
    }
}
