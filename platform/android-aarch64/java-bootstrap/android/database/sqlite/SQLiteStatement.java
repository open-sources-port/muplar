package android.database.sqlite;

import java.util.Map;
import java.util.TreeMap;

public class SQLiteStatement implements AutoCloseable {
    private final SQLiteDatabase database;
    private final String sql;
    private final Map<Integer, Object> bindings = new TreeMap<Integer, Object>();

    SQLiteStatement(SQLiteDatabase database, String sql) {
        this.database = database;
        this.sql = sql == null ? "" : sql;
    }

    public void bindNull(int index) { bindings.put(index, null); }
    public void bindLong(int index, long value) { bindings.put(index, value); }
    public void bindDouble(int index, double value) { bindings.put(index, value); }
    public void bindString(int index, String value) { bindings.put(index, value); }
    public void bindBlob(int index, byte[] value) { bindings.put(index, value); }
    public void clearBindings() { bindings.clear(); }
    public void execute() { database.execSQL(sql, bindings.values().toArray()); }
    public long executeInsert() { execute(); return 1; }
    public int executeUpdateDelete() { execute(); return 0; }
    public long simpleQueryForLong() {
        return database.simpleQueryForLong(sql, stringBindings());
    }
    public String simpleQueryForString() {
        return database.simpleQueryForString(sql, stringBindings());
    }
    private String[] stringBindings() {
        String[] values = new String[bindings.size()];
        int index = 0;
        for (Object value : bindings.values())
            values[index++] = value == null ? null : String.valueOf(value);
        return values;
    }
    public void close() { bindings.clear(); }
}
