package android.os;

public final class UserHandle {
    public static final UserHandle CURRENT = new UserHandle(myUserId());
    private final int identifier;
    public UserHandle(int identifier) { this.identifier = identifier; }
    public static UserHandle of(int identifier) { return new UserHandle(identifier); }
    public int getIdentifier() { return identifier; }
    public static int myUserId() {
        try {
            return Integer.parseInt(System.getProperty("muplar.user.id", "0"));
        } catch (NumberFormatException error) {
            return 0;
        }
    }
    @Override public boolean equals(Object other) {
        return other instanceof UserHandle &&
            ((UserHandle)other).identifier == identifier;
    }
    @Override public int hashCode() { return identifier; }
}
