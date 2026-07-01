package android.net;

public final class Uri {
    private final String value;
    private Uri(String value) { this.value = value == null ? "" : value; }
    public static Uri parse(String value) { return new Uri(value); }
    public String getLastPathSegment() {
        int end = value.length();
        while (end > 0 && value.charAt(end - 1) == '/') --end;
        int slash = value.lastIndexOf('/', end - 1);
        return value.substring(slash + 1, end);
    }
    @Override public String toString() { return value; }
    @Override public boolean equals(Object other) {
        return other instanceof Uri && value.equals(((Uri)other).value);
    }
    @Override public int hashCode() { return value.hashCode(); }
}
