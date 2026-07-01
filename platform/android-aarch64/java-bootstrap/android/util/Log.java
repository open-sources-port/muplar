package android.util;

public final class Log {
    public static final int VERBOSE = 2;
    public static final int DEBUG = 3;
    public static final int INFO = 4;
    public static final int WARN = 5;
    public static final int ERROR = 6;
    public static final int ASSERT = 7;

    private Log() {}

    public static int v(String tag, String message) { return print("V", tag, message, null); }
    public static int d(String tag, String message) { return print("D", tag, message, null); }
    public static int i(String tag, String message) { return print("I", tag, message, null); }
    public static int w(String tag, String message) { return print("W", tag, message, null); }
    public static int e(String tag, String message) { return print("E", tag, message, null); }
    public static int wtf(String tag, String message) { return print("A", tag, message, null); }
    public static int v(String tag, String message, Throwable error) { return print("V", tag, message, error); }
    public static int d(String tag, String message, Throwable error) { return print("D", tag, message, error); }
    public static int i(String tag, String message, Throwable error) { return print("I", tag, message, error); }
    public static int w(String tag, String message, Throwable error) { return print("W", tag, message, error); }
    public static int e(String tag, String message, Throwable error) { return print("E", tag, message, error); }
    public static int wtf(String tag, String message, Throwable error) { return print("A", tag, message, error); }
    public static int w(String tag, Throwable error) { return print("W", tag, "", error); }
    public static boolean isLoggable(String tag, int level) { return true; }
    public static String getStackTraceString(Throwable error) {
        if (error == null) return "";
        java.io.StringWriter output = new java.io.StringWriter();
        error.printStackTrace(new java.io.PrintWriter(output));
        return output.toString();
    }
    public static int println(int priority, String tag, String message) {
        return print(String.valueOf(priority), tag, message, null);
    }

    private static int print(String level, String tag, String message,
                             Throwable error) {
        String line = "[" + level + "/" + tag + "] " +
            (message == null ? "null" : message);
        if (level.equals("E") || level.equals("A") || level.equals("W"))
            System.err.println(line);
        else
            System.out.println(line);
        if (error != null) error.printStackTrace(System.err);
        return line.length();
    }
}
