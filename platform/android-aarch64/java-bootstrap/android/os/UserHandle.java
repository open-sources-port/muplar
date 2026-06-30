package android.os;

public final class UserHandle {
    public static final UserHandle CURRENT = new UserHandle(0);
    private final int identifier;
    public UserHandle(int identifier) { this.identifier = identifier; }
    public int getIdentifier() { return identifier; }
    public static int myUserId() { return 0; }
}
