package android.database.sqlite;

import android.content.ContentValues;
import android.database.Cursor;
import android.database.MatrixCursor;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.ObjectInputStream;
import java.io.ObjectOutputStream;
import java.io.Serializable;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class SQLiteDatabase {
    public static final int OPEN_READWRITE = 0;
    public static final int OPEN_READONLY = 1;
    public static final int NO_LOCALIZED_COLLATORS = 16;
    public static final int CONFLICT_REPLACE = 5;
    public interface CursorFactory {}

    private static final String[] FAVORITES_COLUMNS = {
        "_id", "title", "intent", "container", "screen", "cellX", "cellY",
        "spanX", "spanY", "itemType", "appWidgetId", "iconPackage",
        "iconResource", "icon", "appWidgetProvider", "modified", "restored",
        "profileId", "rank", "options", "appWidgetSource"
    };
    private static final Pattern CREATE_TABLE = Pattern.compile(
        "(?is)^\\s*CREATE\\s+TABLE(?:\\s+IF\\s+NOT\\s+EXISTS)?\\s+[`\"]?([\\w.]+)");
    private static final Pattern DROP_TABLE = Pattern.compile(
        "(?is)^\\s*DROP\\s+TABLE(?:\\s+IF\\s+EXISTS)?\\s+[`\"]?([\\w.]+)");

    private static final class State implements Serializable {
        private static final long serialVersionUID = 1L;
        int version;
        final Map<String, List<Map<String, Object>>> tables = new LinkedHashMap<>();
        final Map<String, LinkedHashSet<String>> columns = new LinkedHashMap<>();
    }

    private final File file;
    private State state;
    private boolean open = true;
    private int transactionDepth;
    private boolean transactionSuccessful;
    private boolean dirty;

    public SQLiteDatabase() { this(null); }
    SQLiteDatabase(File file) {
        this.file = file;
        this.state = load(file);
    }

    public void execSQL(String sql) { execSQL(sql, new Object[0]); }
    public synchronized void execSQL(String sql, Object[] bindArgs) {
        ensureOpen();
        if (sql == null) return;
        Matcher create = CREATE_TABLE.matcher(sql);
        if (create.find()) {
            String table = normalize(create.group(1));
            table(table);
            LinkedHashSet<String> columns = state.columns.computeIfAbsent(
                table, ignored -> new LinkedHashSet<String>());
            int left = sql.indexOf('(');
            int right = sql.lastIndexOf(')');
            if (left >= 0 && right > left) {
                for (String definition : sql.substring(left + 1, right).split(",")) {
                    String value = definition.trim();
                    if (value.isEmpty()) continue;
                    String column = value.split("\\s+", 2)[0].replace("`", "")
                        .replace("\"", "");
                    String upper = column.toUpperCase(Locale.ROOT);
                    if (!upper.equals("PRIMARY") && !upper.equals("UNIQUE") &&
                            !upper.equals("CONSTRAINT") && !upper.equals("FOREIGN"))
                        columns.add(column);
                }
            }
            changed();
            return;
        }
        Matcher drop = DROP_TABLE.matcher(sql);
        if (drop.find()) {
            String table = normalize(drop.group(1));
            state.tables.remove(table);
            state.columns.remove(table);
            changed();
            return;
        }
        String upper = sql.trim().toUpperCase(Locale.ROOT);
        if (upper.startsWith("DELETE FROM ")) {
            String table = normalize(sql.trim().substring(12).split("\\s+", 2)[0]);
            List<Map<String, Object>> rows = table(table);
            rows.clear();
            changed();
        }
    }

    public SQLiteStatement compileStatement(String sql) {
        return new SQLiteStatement(this, sql);
    }

    public synchronized long insert(String table, String nullColumnHack,
            ContentValues values) {
        ensureOpen();
        String name = normalize(table);
        Map<String, Object> row = copy(values);
        LinkedHashSet<String> columns = state.columns.computeIfAbsent(
            name, ignored -> new LinkedHashSet<String>());
        columns.addAll(row.keySet());
        if (!row.containsKey("_id")) row.put("_id", nextId(name));
        table(name).add(row);
        changed();
        Object id = row.get("_id");
        return id instanceof Number ? ((Number) id).longValue() : 1;
    }

    public long insertOrThrow(String table, String nullColumnHack, ContentValues values) {
        return insert(table, nullColumnHack, values);
    }
    public synchronized long insertWithOnConflict(String table, String nullColumnHack,
            ContentValues values, int conflictAlgorithm) {
        if (conflictAlgorithm == CONFLICT_REPLACE && values.containsKey("_id")) {
            Object id = values.get("_id");
            delete(table, "_id=?", new String[]{String.valueOf(id)});
        }
        return insert(table, nullColumnHack, values);
    }
    public long replace(String table, String nullColumnHack, ContentValues values) {
        return insertWithOnConflict(table, nullColumnHack, values, CONFLICT_REPLACE);
    }

    public synchronized int update(String table, ContentValues values,
            String where, String[] args) {
        int count = 0;
        Map<String, Object> replacement = copy(values);
        for (Map<String, Object> row : table(normalize(table))) {
            if (!matches(row, where, args)) continue;
            row.putAll(replacement);
            count++;
        }
        if (count > 0) changed();
        return count;
    }

    public synchronized int delete(String table, String where, String[] args) {
        List<Map<String, Object>> rows = table(normalize(table));
        int before = rows.size();
        rows.removeIf(row -> matches(row, where, args));
        int count = before - rows.size();
        if (count > 0) changed();
        return count;
    }

    public synchronized Cursor query(String table, String[] columns, String selection,
            String[] selectionArgs, String groupBy, String having, String orderBy) {
        String name = normalize(table);
        String[] projection = columns;
        if (projection == null) {
            Set<String> known = state.columns.get(name);
            projection = known == null || known.isEmpty()
                ? ("favorites".equals(name) ? FAVORITES_COLUMNS : new String[0])
                : known.toArray(new String[0]);
        }
        List<Map<String, Object>> selected = new ArrayList<>();
        for (Map<String, Object> row : table(name))
            if (matches(row, selection, selectionArgs)) selected.add(row);
        sort(selected, orderBy);
        MatrixCursor cursor = new MatrixCursor(projection);
        for (Map<String, Object> row : selected) {
            Object[] values = new Object[projection.length];
            for (int i = 0; i < projection.length; i++) values[i] = row.get(projection[i]);
            cursor.addRow(values);
        }
        return cursor;
    }

    public Cursor query(boolean distinct, String table, String[] columns,
            String selection, String[] selectionArgs, String groupBy, String having,
            String orderBy, String limit, android.os.CancellationSignal cancellation) {
        if (cancellation != null) cancellation.throwIfCanceled();
        return query(table, columns, selection, selectionArgs, groupBy, having, orderBy);
    }
    public Cursor query(boolean distinct, String table, String[] columns,
            String selection, String[] selectionArgs, String groupBy, String having,
            String orderBy, String limit) {
        return query(distinct, table, columns, selection, selectionArgs, groupBy,
            having, orderBy, limit, null);
    }

    public synchronized Cursor rawQuery(String sql, String[] selectionArgs) {
        if (sql == null) return new MatrixCursor(new String[0]);
        if (sql.toLowerCase(Locale.ROOT).contains("sqlite_master")) {
            boolean countQuery = sql.toUpperCase(Locale.ROOT).contains("COUNT(");
            String wanted = selectionArgs != null && selectionArgs.length > 0
                ? normalize(selectionArgs[selectionArgs.length - 1]) : null;
            if (wanted == null) {
                Matcher literal = Pattern.compile(
                    "(?i)(?:name|tbl_name)\\s*=\\s*'([^']+)'").matcher(sql);
                if (literal.find()) wanted = normalize(literal.group(1));
            }
            List<String> names = new ArrayList<String>();
            for (String table : state.tables.keySet())
                if (wanted == null || wanted.equals(table)) names.add(table);
            MatrixCursor cursor = new MatrixCursor(
                new String[]{countQuery ? "COUNT(*)" : "name"});
            if (countQuery) cursor.addRow(new Object[]{names.size()});
            else for (String name : names) cursor.addRow(new Object[]{name});
            return cursor;
        }
        Matcher count = Pattern.compile(
            "(?is)SELECT\\s+COUNT\\s*\\(\\s*\\*\\s*\\)\\s+FROM\\s+([\\w.]+)")
            .matcher(sql);
        if (count.find()) {
            MatrixCursor cursor = new MatrixCursor(new String[]{"COUNT(*)"});
            cursor.addRow(new Object[]{table(normalize(count.group(1))).size()});
            return cursor;
        }
        Matcher max = Pattern.compile(
            "(?is)SELECT\\s+MAX\\s*\\(\\s*([\\w.]+)\\s*\\)\\s+FROM\\s+([\\w.]+)")
            .matcher(sql);
        if (max.find()) {
            long value = 0;
            for (Map<String, Object> row : table(normalize(max.group(2)))) {
                Object item = row.get(max.group(1));
                if (item instanceof Number) value = Math.max(value, ((Number) item).longValue());
            }
            MatrixCursor cursor = new MatrixCursor(new String[]{"MAX(" + max.group(1) + ")"});
            cursor.addRow(new Object[]{value});
            return cursor;
        }
        return new MatrixCursor(new String[0]);
    }

    long simpleQueryForLong(String sql, String[] args) {
        Cursor cursor = rawQuery(sql, args);
        try { return cursor.moveToFirst() ? cursor.getLong(0) : 0; }
        finally { cursor.close(); }
    }
    String simpleQueryForString(String sql, String[] args) {
        Cursor cursor = rawQuery(sql, args);
        try { return cursor.moveToFirst() ? cursor.getString(0) : null; }
        finally { cursor.close(); }
    }

    public synchronized void beginTransaction() {
        ensureOpen();
        transactionDepth++;
        transactionSuccessful = false;
    }
    public synchronized void setTransactionSuccessful() { transactionSuccessful = true; }
    public synchronized void endTransaction() {
        if (transactionDepth > 0) transactionDepth--;
        if (transactionDepth == 0 && transactionSuccessful && dirty) save();
        transactionSuccessful = false;
    }
    public boolean isOpen() { return open; }
    public synchronized void close() { if (dirty) save(); open = false; }
    public int getVersion() { return state.version; }
    public void setVersion(int version) { state.version = version; changed(); }
    boolean hasPersistentState() { return file != null && file.isFile(); }

    private List<Map<String, Object>> table(String name) {
        return state.tables.computeIfAbsent(name, ignored -> new ArrayList<Map<String, Object>>());
    }
    private long nextId(String table) {
        long next = 1;
        for (Map<String, Object> row : table(table)) {
            Object id = row.get("_id");
            if (id instanceof Number) next = Math.max(next, ((Number) id).longValue() + 1);
        }
        return next;
    }
    private static Map<String, Object> copy(ContentValues values) {
        Map<String, Object> result = new LinkedHashMap<>();
        if (values != null)
            for (Map.Entry<String, Object> entry : values.valueSet())
                result.put(entry.getKey(), cloneValue(entry.getValue()));
        return result;
    }
    private static Object cloneValue(Object value) {
        return value instanceof byte[] ? ((byte[]) value).clone() : value;
    }
    private static String normalize(String value) {
        if (value == null) return "";
        String result = value.trim().replace("`", "").replace("\"", "");
        int dot = result.lastIndexOf('.');
        return dot < 0 ? result : result.substring(dot + 1);
    }
    private static boolean matches(Map<String, Object> row, String where, String[] args) {
        if (where == null || where.trim().isEmpty()) return true;
        String expression = where.trim().replace("(", "").replace(")", "");
        String[] clauses = expression.split("(?i)\\s+AND\\s+");
        int argument = 0;
        for (String clause : clauses) {
            String value = clause.trim();
            Matcher nullCheck = Pattern.compile("(?i)([\\w.]+)\\s+IS\\s+(NOT\\s+)?NULL")
                .matcher(value);
            if (nullCheck.matches()) {
                boolean isNull = row.get(normalize(nullCheck.group(1))) == null;
                if (nullCheck.group(2) == null ? !isNull : isNull) return false;
                continue;
            }
            Matcher in = Pattern.compile("(?i)([\\w.]+)\\s+IN\\s*\\(([^)]*)\\)")
                .matcher(value);
            if (in.matches()) {
                Object actual = row.get(normalize(in.group(1)));
                boolean found = false;
                for (String candidate : in.group(2).split(",")) {
                    String expected = candidate.trim();
                    if ("?".equals(expected))
                        expected = args != null && argument < args.length
                            ? args[argument++] : null;
                    else if (expected.startsWith("'") && expected.endsWith("'"))
                        expected = expected.substring(1, expected.length() - 1);
                    if (compare(actual, expected) == 0) found = true;
                }
                if (!found) return false;
                continue;
            }
            Matcher comparison = Pattern.compile("(?i)([\\w.]+)\\s*(=|!=|<>|>=|<=|>|<)\\s*(\\?|[-+]?\\d+|'[^']*')")
                .matcher(value);
            if (!comparison.matches()) return false;
            Object actual = row.get(normalize(comparison.group(1)));
            String expectedText = comparison.group(3);
            if ("?".equals(expectedText))
                expectedText = args != null && argument < args.length ? args[argument++] : null;
            else if (expectedText.startsWith("'"))
                expectedText = expectedText.substring(1, expectedText.length() - 1);
            int order = compare(actual, expectedText);
            String operator = comparison.group(2);
            if (("=".equals(operator) && order != 0) ||
                    (("!=".equals(operator) || "<>".equals(operator)) && order == 0) ||
                    (">".equals(operator) && order <= 0) || ("<".equals(operator) && order >= 0) ||
                    (">=".equals(operator) && order < 0) || ("<=".equals(operator) && order > 0))
                return false;
        }
        return true;
    }
    private static int compare(Object actual, String expected) {
        if (actual == null) return expected == null ? 0 : -1;
        if (actual instanceof Number && expected != null) {
            try { return Double.compare(((Number) actual).doubleValue(), Double.parseDouble(expected)); }
            catch (NumberFormatException ignored) {}
        }
        return String.valueOf(actual).compareTo(expected == null ? "" : expected);
    }
    private static void sort(List<Map<String, Object>> rows, String orderBy) {
        if (orderBy == null || orderBy.trim().isEmpty()) return;
        String[] parts = orderBy.trim().split("\\s+");
        String column = normalize(parts[0]);
        boolean descending = parts.length > 1 && "DESC".equalsIgnoreCase(parts[1]);
        Collections.sort(rows, new Comparator<Map<String, Object>>() {
            public int compare(Map<String, Object> left, Map<String, Object> right) {
                int value = String.valueOf(left.get(column)).compareTo(
                    String.valueOf(right.get(column)));
                return descending ? -value : value;
            }
        });
    }
    private void changed() { dirty = true; if (transactionDepth == 0) save(); }
    private void ensureOpen() { if (!open) throw new IllegalStateException("database is closed"); }
    private synchronized void save() {
        if (file == null) { dirty = false; return; }
        try {
            File parent = file.getParentFile();
            if (parent != null && !parent.isDirectory() && !parent.mkdirs())
                throw new IllegalStateException("cannot create " + parent);
            File temporary = new File(file.getPath() + ".tmp");
            try (ObjectOutputStream output = new ObjectOutputStream(
                    new FileOutputStream(temporary))) { output.writeObject(state); }
            try {
                Files.move(temporary.toPath(), file.toPath(),
                    StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
            } catch (java.nio.file.AtomicMoveNotSupportedException ignored) {
                Files.move(temporary.toPath(), file.toPath(),
                    StandardCopyOption.REPLACE_EXISTING);
            }
            dirty = false;
        } catch (Exception error) {
            throw new SQLiteException("cannot persist database " + file + ": " + error);
        }
    }
    private static State load(File file) {
        if (file == null || !file.isFile()) return new State();
        try (ObjectInputStream input = new ObjectInputStream(new FileInputStream(file))) {
            Object value = input.readObject();
            return value instanceof State ? (State) value : new State();
        } catch (Exception error) {
            File corrupt = new File(file.getPath() + ".corrupt");
            file.renameTo(corrupt);
            return new State();
        }
    }

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
